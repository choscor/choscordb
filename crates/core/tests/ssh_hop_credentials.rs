use choscordb_core::{
    ConnectionProfile, CredentialUpdate, CredentialUpdates, Engine, EngineConfig, Event,
};
use choscordb_credentials::{CredentialError, CredentialStore, Secret};
use std::{
    collections::HashMap,
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};

#[derive(Default)]
struct Vault {
    items: Mutex<HashMap<String, String>>,
    reject: Mutex<Option<String>>,
}
impl CredentialStore for Vault {
    fn get(&self, key: &str) -> choscordb_credentials::Result<Secret> {
        self.items
            .lock()
            .unwrap()
            .get(key)
            .cloned()
            .map(Secret::new)
            .ok_or(CredentialError::Missing)
    }
    fn put(&self, key: &str, value: &Secret) -> choscordb_credentials::Result<()> {
        if self.reject.lock().unwrap().as_deref() == Some(value.expose()) {
            return Err(CredentialError::Unavailable);
        }
        self.items
            .lock()
            .unwrap()
            .insert(key.into(), value.expose().into());
        Ok(())
    }
    fn delete(&self, key: &str) -> choscordb_credentials::Result<()> {
        self.items.lock().unwrap().remove(key);
        Ok(())
    }
}
fn event(engine: &mut Engine) -> Event {
    let until = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(event) = engine.try_event() {
            return event;
        }
        assert!(Instant::now() < until);
        std::thread::sleep(Duration::from_millis(2));
    }
}
fn profile() -> ConnectionProfile {
    serde_json::from_value(serde_json::json!({"id":"hops","name":"Hops","group_id":null,"credential_ref":null,
      "configuration":{"driver":"postgres","host":"localhost","port":5432,"user":"user","database":"db",
      "tls":{"mode":"VerifyFull","root_certificate_path":null},
      "ssh":{"host":"target.example","port":22,"user":"target","authentication":"agent","options":{"jump_hosts":[
        {"id":"first","host":"first.example","port":22,"user":"one","authentication":"password"},
        {"id":"second","host":"second.example","port":22,"user":"two","authentication":"password"}
      ]}}}})).unwrap()
}
fn updates() -> CredentialUpdates {
    CredentialUpdates {
        ssh_private_key: CredentialUpdate::Keep,
        ssh_jump_private_keys: Default::default(),
        database: CredentialUpdate::Keep,
        ssh: CredentialUpdate::Keep,
        tls: CredentialUpdate::Keep,
        proxy: CredentialUpdate::Keep,
        ssh_jumps: Default::default(),
    }
}
fn inline_profile() -> ConnectionProfile {
    let mut profile = profile();
    let choscordb_core::ProfileConfiguration::Postgres { ssh: Some(ssh), .. } =
        &mut profile.configuration
    else {
        panic!()
    };
    ssh.authentication = choscordb_driver_api::SshAuthentication::PublicKey;
    ssh.identity_source = choscordb_driver_api::SshIdentitySource::Inline;
    ssh.options.jump_hosts[0].authentication =
        choscordb_driver_api::SshJumpAuthentication::PublicKey;
    ssh.options.jump_hosts[0].identity_source = choscordb_driver_api::SshIdentitySource::Inline;
    profile
}

#[test]
fn inline_private_keys_have_independent_refs_and_follow_stable_hop_ids() {
    let vault = Arc::new(Vault::default());
    let directory = tempfile::tempdir().unwrap();
    let metadata = directory.path().join("profiles.sqlite");
    let mut engine = Engine::new_with_credentials(
        EngineConfig {
            storage_path: Some(metadata.clone()),
            ..Default::default()
        },
        vec![],
        vault.clone(),
    )
    .unwrap();
    let mut update = updates();
    update.ssh_private_key = CredentialUpdate::Replace(Secret::new("target-key"));
    update.ssh_jump_private_keys.insert(
        "first".into(),
        CredentialUpdate::Replace(Secret::new("hop-key")),
    );
    update
        .ssh_jump_private_keys
        .insert("second".into(), CredentialUpdate::Keep);
    engine
        .profile_save_with_secrets(inline_profile(), update, 1)
        .unwrap();
    let Event::ProfileSaved { profile: saved, .. } = event(&mut engine) else {
        panic!("save failed")
    };
    let target_ref = saved.ssh_private_key_ref.clone().expect("target ref");
    let hop_ref = saved
        .ssh_jump_private_key_refs
        .get("first")
        .cloned()
        .expect("hop ref");
    assert_ne!(target_ref, hop_ref);
    assert_eq!(vault.get(&target_ref).unwrap().expose(), "target-key");
    assert_eq!(vault.get(&hop_ref).unwrap().expose(), "hop-key");
    let serialized = serde_json::to_string(&saved).unwrap();
    assert!(!serialized.contains("target-key"));
    assert!(!serialized.contains("hop-key"));
    let metadata_bytes = std::fs::read(&metadata).unwrap();
    assert!(
        !metadata_bytes
            .windows(b"target-key".len())
            .any(|window| window == b"target-key")
    );
    assert!(
        !metadata_bytes
            .windows(b"hop-key".len())
            .any(|window| window == b"hop-key")
    );
    let wal = metadata.with_extension("sqlite-wal");
    if wal.exists() {
        let wal_bytes = std::fs::read(wal).unwrap();
        assert!(
            !wal_bytes
                .windows(b"target-key".len())
                .any(|window| window == b"target-key")
        );
        assert!(
            !wal_bytes
                .windows(b"hop-key".len())
                .any(|window| window == b"hop-key")
        );
    }
    let mut draft = *saved;
    draft.ssh_private_key_ref = Some("unowned-target".into());
    draft
        .ssh_jump_private_key_refs
        .insert("first".into(), "unowned-hop".into());
    let choscordb_core::ProfileConfiguration::Postgres { ssh: Some(ssh), .. } =
        &mut draft.configuration
    else {
        panic!()
    };
    ssh.options.jump_hosts.reverse();
    engine
        .profile_save_with_secrets(draft, updates(), 2)
        .unwrap();
    let Event::ProfileSaved { profile: saved, .. } = event(&mut engine) else {
        panic!()
    };
    assert_eq!(saved.ssh_private_key_ref.as_ref(), Some(&target_ref));
    assert_eq!(saved.ssh_jump_private_key_refs.get("first"), Some(&hop_ref));
    let mut removed = *saved;
    let choscordb_core::ProfileConfiguration::Postgres { ssh: Some(ssh), .. } =
        &mut removed.configuration
    else {
        panic!()
    };
    ssh.options
        .jump_hosts
        .retain(|hop| hop.id.as_deref() != Some("first"));
    engine
        .profile_save_with_secrets(removed, updates(), 3)
        .unwrap();
    let Event::ProfileSaved { profile: saved, .. } = event(&mut engine) else {
        panic!()
    };
    assert!(saved.ssh_jump_private_key_refs.is_empty());
    assert!(matches!(vault.get(&hop_ref), Err(CredentialError::Missing)));
    engine.profile_delete("hops".into(), 4).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileDeleted { .. }));
    assert!(matches!(
        vault.get(&target_ref),
        Err(CredentialError::Missing)
    ));
}

type ObservedInlineKeys = Vec<(Option<String>, Option<String>)>;
struct InlineDriver {
    seen: Arc<Mutex<ObservedInlineKeys>>,
}
#[async_trait::async_trait]
impl choscordb_driver_api::DatabaseDriver for InlineDriver {
    fn id(&self) -> &'static str {
        "postgres"
    }
    fn capabilities(&self) -> choscordb_driver_api::DriverCapabilities {
        Default::default()
    }
    async fn connect(
        &self,
        options: choscordb_driver_api::ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn choscordb_driver_api::Connection>> {
        use choscordb_driver_api::*;
        let ConnectionOptions::Postgres {
            ssh_private_key,
            ssh_jump_private_keys,
            ..
        } = options
        else {
            panic!()
        };
        self.seen.lock().unwrap().push((
            ssh_private_key.map(|s| s.expose().to_owned()),
            ssh_jump_private_keys
                .get("first")
                .map(|s| s.expose().to_owned()),
        ));
        choscordb_driver_sqlite::SqliteDriver
            .connect(ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            })
            .await
    }
}

#[test]
fn inline_keys_resolve_for_test_and_connect_with_independent_transient_overrides() {
    let vault = Arc::new(Vault::default());
    let seen = Arc::new(Mutex::new(Vec::new()));
    let mut engine = Engine::new_with_credentials(
        EngineConfig::default(),
        vec![Arc::new(InlineDriver { seen: seen.clone() })],
        vault,
    )
    .unwrap();
    let mut update = updates();
    update.ssh_private_key = CredentialUpdate::Replace(Secret::new("saved-target"));
    update.ssh_jump_private_keys.insert(
        "first".into(),
        CredentialUpdate::Replace(Secret::new("saved-hop")),
    );
    engine
        .profile_save_with_secrets(inline_profile(), update, 1)
        .unwrap();
    let Event::ProfileSaved { profile: saved, .. } = event(&mut engine) else {
        panic!()
    };
    engine.test_profile((*saved).clone(), None, 2).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
    let mut transient = choscordb_core::ProfileSecrets {
        ssh_private_key: Some(Secret::new("transient-target")),
        ..Default::default()
    };
    transient
        .ssh_jump_private_keys
        .insert("first".into(), Secret::new("transient-hop"));
    engine
        .connect_profile_with_secrets(*saved, transient)
        .unwrap();
    assert!(matches!(event(&mut engine), Event::Connected { .. }));
    assert_eq!(
        *seen.lock().unwrap(),
        vec![
            (Some("saved-target".into()), Some("saved-hop".into())),
            (
                Some("transient-target".into()),
                Some("transient-hop".into())
            ),
        ]
    );
}

#[test]
fn failed_inline_hop_key_write_discards_new_target_key_and_preserves_saved_refs() {
    let vault = Arc::new(Vault::default());
    let mut engine =
        Engine::new_with_credentials(EngineConfig::default(), vec![], vault.clone()).unwrap();
    let mut initial = updates();
    initial.ssh_private_key = CredentialUpdate::Replace(Secret::new("old-target"));
    initial.ssh_jump_private_keys.insert(
        "first".into(),
        CredentialUpdate::Replace(Secret::new("old-hop")),
    );
    engine
        .profile_save_with_secrets(inline_profile(), initial, 1)
        .unwrap();
    let Event::ProfileSaved { profile: saved, .. } = event(&mut engine) else {
        panic!()
    };
    *vault.reject.lock().unwrap() = Some("reject-hop".into());
    let mut update = updates();
    update.ssh_private_key = CredentialUpdate::Replace(Secret::new("new-target"));
    update.ssh_jump_private_keys.insert(
        "first".into(),
        CredentialUpdate::Replace(Secret::new("reject-hop")),
    );
    engine
        .profile_save_with_secrets(*saved.clone(), update, 2)
        .unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileFailed { .. }));
    engine.profile_list(3).unwrap();
    let Event::Profiles { profiles, .. } = event(&mut engine) else {
        panic!()
    };
    assert_eq!(profiles[0].ssh_private_key_ref, saved.ssh_private_key_ref);
    assert_eq!(
        profiles[0].ssh_jump_private_key_refs,
        saved.ssh_jump_private_key_refs
    );
    assert_eq!(vault.items.lock().unwrap().len(), 2);
}
#[test]
fn hop_credentials_are_independent_and_keep_uses_owned_stable_ids() {
    let vault = Arc::new(Vault::default());
    let mut engine =
        Engine::new_with_credentials(EngineConfig::default(), vec![], vault.clone()).unwrap();
    let mut update = updates();
    update.ssh_jumps.insert(
        "first".into(),
        CredentialUpdate::Replace(Secret::new("first-password")),
    );
    update.ssh_jumps.insert(
        "second".into(),
        CredentialUpdate::Replace(Secret::new("second-password")),
    );
    engine
        .profile_save_with_secrets(profile(), update, 1)
        .unwrap();
    let Event::ProfileSaved { profile: saved, .. } = event(&mut engine) else {
        panic!("save failed")
    };
    let first = saved
        .ssh_jump_credential_refs
        .get("first")
        .expect("first hop credential missing")
        .clone();
    let second = saved
        .ssh_jump_credential_refs
        .get("second")
        .expect("second hop credential missing")
        .clone();
    assert_ne!(first, second);
    assert_eq!(vault.get(&first).unwrap().expose(), "first-password");
    assert_eq!(vault.get(&second).unwrap().expose(), "second-password");
    let mut draft = *saved;
    let choscordb_core::ProfileConfiguration::Postgres { ssh: Some(ssh), .. } =
        &mut draft.configuration
    else {
        panic!()
    };
    ssh.options.jump_hosts.reverse();
    draft
        .ssh_jump_credential_refs
        .insert("first".into(), "unowned-reference".into());
    engine
        .profile_save_with_secrets(draft, updates(), 2)
        .unwrap();
    let Event::ProfileSaved { profile: saved, .. } = event(&mut engine) else {
        panic!("keep failed")
    };
    assert_eq!(saved.ssh_jump_credential_refs.get("first"), Some(&first));
    assert_eq!(saved.ssh_jump_credential_refs.get("second"), Some(&second));
    assert_eq!(vault.items.lock().unwrap().len(), 2);
    let mut removed = *saved;
    let choscordb_core::ProfileConfiguration::Postgres { ssh: Some(ssh), .. } =
        &mut removed.configuration
    else {
        panic!()
    };
    ssh.options
        .jump_hosts
        .retain(|hop| hop.id.as_deref() != Some("first"));
    engine
        .profile_save_with_secrets(removed, updates(), 3)
        .unwrap();
    let Event::ProfileSaved { profile: saved, .. } = event(&mut engine) else {
        panic!("remove failed")
    };
    assert!(!saved.ssh_jump_credential_refs.contains_key("first"));
    assert_eq!(saved.ssh_jump_credential_refs.get("second"), Some(&second));
    assert!(matches!(vault.get(&first), Err(CredentialError::Missing)));
    engine
        .profile_duplicate("hops".into(), "copy".into(), "Copy".into(), 4)
        .unwrap();
    let Event::ProfileSaved { profile: copy, .. } = event(&mut engine) else {
        panic!("duplicate failed")
    };
    assert!(copy.ssh_jump_credential_refs.is_empty());
    engine.profile_delete("hops".into(), 5).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileDeleted { .. }));
    assert!(matches!(vault.get(&second), Err(CredentialError::Missing)));
}

type ObservedHopSecrets = Vec<(Option<String>, Option<String>)>;
struct HopDriver {
    seen: Arc<Mutex<ObservedHopSecrets>>,
}
#[async_trait::async_trait]
impl choscordb_driver_api::DatabaseDriver for HopDriver {
    fn id(&self) -> &'static str {
        "postgres"
    }
    fn capabilities(&self) -> choscordb_driver_api::DriverCapabilities {
        Default::default()
    }
    async fn connect(
        &self,
        options: choscordb_driver_api::ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn choscordb_driver_api::Connection>> {
        use choscordb_driver_api::*;
        let ConnectionOptions::Postgres {
            ssh_jump_secrets, ..
        } = options
        else {
            panic!()
        };
        self.seen.lock().unwrap().push((
            ssh_jump_secrets.get("first").map(|s| s.expose().to_owned()),
            ssh_jump_secrets
                .get("second")
                .map(|s| s.expose().to_owned()),
        ));
        choscordb_driver_sqlite::SqliteDriver
            .connect(ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            })
            .await
    }
}
#[test]
fn saved_hop_secrets_resolve_for_test_and_connect_and_transient_overrides() {
    let vault = Arc::new(Vault::default());
    let seen = Arc::new(Mutex::new(Vec::new()));
    let mut engine = Engine::new_with_credentials(
        EngineConfig::default(),
        vec![Arc::new(HopDriver { seen: seen.clone() })],
        vault.clone(),
    )
    .unwrap();
    let mut update = updates();
    update.ssh_jumps.insert(
        "first".into(),
        CredentialUpdate::Replace(Secret::new("one")),
    );
    update.ssh_jumps.insert(
        "second".into(),
        CredentialUpdate::Replace(Secret::new("two")),
    );
    engine
        .profile_save_with_secrets(profile(), update, 1)
        .unwrap();
    let Event::ProfileSaved { profile: saved, .. } = event(&mut engine) else {
        panic!()
    };
    let mut edited = (*saved).clone();
    let choscordb_core::ProfileConfiguration::Postgres { ssh: Some(ssh), .. } =
        &mut edited.configuration
    else {
        panic!()
    };
    ssh.options
        .jump_hosts
        .retain(|hop| hop.id.as_deref() != Some("first"));
    engine.test_profile(edited.clone(), None, 3).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
    engine.connect_profile(edited, None).unwrap();
    assert!(matches!(event(&mut engine), Event::Connected { .. }));
    let mut switched = (*saved).clone();
    let choscordb_core::ProfileConfiguration::Postgres { ssh: Some(ssh), .. } =
        &mut switched.configuration
    else {
        panic!()
    };
    ssh.options.jump_hosts[0].authentication = choscordb_driver_api::SshJumpAuthentication::Agent;
    engine.test_profile(switched, None, 4).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
    engine.test_profile((*saved).clone(), None, 2).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
    let mut secrets = choscordb_core::ProfileSecrets::default();
    secrets.ssh_jumps.insert("first".into(), Secret::new(""));
    engine
        .connect_profile_with_secrets(*saved, secrets)
        .unwrap();
    assert!(matches!(event(&mut engine), Event::Connected { .. }));
    assert_eq!(
        *seen.lock().unwrap(),
        vec![
            (None, Some("two".into())),
            (None, Some("two".into())),
            (None, Some("two".into())),
            (Some("one".into()), Some("two".into())),
            (Some("".into()), Some("two".into()))
        ]
    );
}

#[test]
fn test_and_connect_reject_unknown_active_hop_ids() {
    let seen = Arc::new(Mutex::new(Vec::new()));
    let mut engine = Engine::new_with_credentials(
        EngineConfig::default(),
        vec![Arc::new(HopDriver { seen: seen.clone() })],
        Arc::new(Vault::default()),
    )
    .unwrap();
    let mut secrets = choscordb_core::ProfileSecrets::default();
    secrets
        .ssh_jumps
        .insert("ghost".into(), Secret::new("secret"));
    assert!(matches!(
        engine.test_profile_with_secrets(profile(), secrets, 1),
        Err(choscordb_core::SubmitError::InvalidInput)
    ));
    let mut secrets = choscordb_core::ProfileSecrets::default();
    secrets
        .ssh_jumps
        .insert("ghost".into(), Secret::new("secret"));
    assert!(matches!(
        engine.connect_profile_with_secrets(profile(), secrets),
        Err(choscordb_core::SubmitError::InvalidInput)
    ));
    assert!(seen.lock().unwrap().is_empty());
}

#[test]
fn later_hop_vault_failure_discards_earlier_new_secret_and_keeps_previous_refs() {
    let vault = Arc::new(Vault::default());
    let mut engine =
        Engine::new_with_credentials(EngineConfig::default(), vec![], vault.clone()).unwrap();
    let mut initial = updates();
    initial.ssh_jumps.insert(
        "first".into(),
        CredentialUpdate::Replace(Secret::new("old-first")),
    );
    initial.ssh_jumps.insert(
        "second".into(),
        CredentialUpdate::Replace(Secret::new("old-second")),
    );
    engine
        .profile_save_with_secrets(profile(), initial, 1)
        .unwrap();
    let Event::ProfileSaved { profile: saved, .. } = event(&mut engine) else {
        panic!()
    };
    *vault.reject.lock().unwrap() = Some("reject-second".into());
    let mut update = updates();
    update.ssh_jumps.insert(
        "first".into(),
        CredentialUpdate::Replace(Secret::new("new-first")),
    );
    update.ssh_jumps.insert(
        "second".into(),
        CredentialUpdate::Replace(Secret::new("reject-second")),
    );
    engine
        .profile_save_with_secrets(*saved.clone(), update, 2)
        .unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileFailed { .. }));
    engine.profile_list(3).unwrap();
    let Event::Profiles { profiles, .. } = event(&mut engine) else {
        panic!()
    };
    assert_eq!(
        profiles[0].ssh_jump_credential_refs,
        saved.ssh_jump_credential_refs
    );
    assert_eq!(vault.items.lock().unwrap().len(), 2);
}

#[test]
fn missing_active_reference_fails_but_inactive_sqlite_secret_is_ignored() {
    let vault = Arc::new(Vault::default());
    let seen = Arc::new(Mutex::new(Vec::new()));
    let mut engine = Engine::new_with_credentials(
        EngineConfig::default(),
        vec![
            Arc::new(HopDriver { seen: seen.clone() }),
            Arc::new(choscordb_driver_sqlite::SqliteDriver),
        ],
        vault,
    )
    .unwrap();
    let mut active = profile();
    active
        .ssh_jump_credential_refs
        .insert("first".into(), "missing-active".into());
    engine.test_profile(active, None, 1).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileFailed { .. }));
    assert!(seen.lock().unwrap().is_empty());

    let sqlite: ConnectionProfile = serde_json::from_value(serde_json::json!({
        "id":"local","name":"Local","group_id":null,"credential_ref":null,
        "configuration":{"driver":"sqlite","path":":memory:","read_only":false}
    }))
    .unwrap();
    let mut secrets = choscordb_core::ProfileSecrets::default();
    secrets
        .ssh_jumps
        .insert("old-hop".into(), Secret::new("unused\nsecret"));
    engine
        .test_profile_with_secrets(sqlite, secrets, 2)
        .unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
}

#[test]
fn metadata_failure_discards_new_hop_secret_and_keeps_old_reference() {
    let directory = tempfile::tempdir().unwrap();
    let metadata = directory.path().join("metadata.sqlite");
    let vault = Arc::new(Vault::default());
    let mut engine = Engine::new_with_credentials(
        EngineConfig {
            storage_path: Some(metadata.clone()),
            ..Default::default()
        },
        vec![],
        vault.clone(),
    )
    .unwrap();
    let mut initial = updates();
    initial.ssh_jumps.insert(
        "first".into(),
        CredentialUpdate::Replace(Secret::new("private-hop-marker-2026")),
    );
    engine
        .profile_save_with_secrets(profile(), initial, 1)
        .unwrap();
    let Event::ProfileSaved { profile: saved, .. } = event(&mut engine) else {
        panic!()
    };
    let old = saved.ssh_jump_credential_refs.get("first").unwrap().clone();
    rusqlite::Connection::open(&metadata).unwrap().execute_batch(
        "CREATE TRIGGER fail_hop_update BEFORE UPDATE ON connection_profiles BEGIN SELECT RAISE(ABORT,'fixture failure'); END;"
    ).unwrap();
    let mut update = updates();
    update.ssh_jumps.insert(
        "first".into(),
        CredentialUpdate::Replace(Secret::new("new")),
    );
    engine.profile_save_with_secrets(*saved, update, 2).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileFailed { .. }));
    assert_eq!(vault.items.lock().unwrap().len(), 1);
    assert_eq!(vault.get(&old).unwrap().expose(), "private-hop-marker-2026");
    for entry in std::fs::read_dir(directory.path()).unwrap() {
        let bytes = std::fs::read(entry.unwrap().path()).unwrap();
        assert!(
            !bytes
                .windows(b"private-hop-marker-2026".len())
                .any(|window| window == b"private-hop-marker-2026")
        );
    }
}
