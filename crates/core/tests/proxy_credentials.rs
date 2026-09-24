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
    serde_json::from_value(
        serde_json::json!({"id":"proxy","name":"Proxy","group_id":null,"credential_ref":null,
        "configuration":{"driver":"sqlite","path":":memory:","read_only":false}}),
    )
    .unwrap()
}
fn updates(proxy: CredentialUpdate) -> CredentialUpdates {
    CredentialUpdates {
        ssh_private_key: CredentialUpdate::Keep,
        ssh_jump_private_keys: Default::default(),
        ssh_jumps: Default::default(),
        database: CredentialUpdate::Keep,
        ssh: CredentialUpdate::Keep,
        tls: CredentialUpdate::Keep,
        proxy,
    }
}
#[test]
fn proxy_password_gets_an_independent_secure_reference_without_plaintext_metadata() {
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
    engine
        .profile_save_with_secrets(
            profile(),
            CredentialUpdates {
                ssh_private_key: CredentialUpdate::Keep,
                ssh_jump_private_keys: Default::default(),
                ssh_jumps: Default::default(),
                database: CredentialUpdate::Replace(Secret::new("database-marker")),
                ssh: CredentialUpdate::Replace(Secret::new("ssh-marker")),
                tls: CredentialUpdate::Replace(Secret::new("tls-marker")),
                proxy: CredentialUpdate::Replace(Secret::new("proxy-secret-marker")),
            },
            1,
        )
        .unwrap();
    let Event::ProfileSaved { profile, .. } = event(&mut engine) else {
        panic!("proxy save failed")
    };
    let json = serde_json::to_value(&profile).unwrap();
    let reference = json["proxy_credential_ref"]
        .as_str()
        .expect("proxy password must have a credential reference");
    assert_eq!(
        vault.get(reference).unwrap().expose(),
        "proxy-secret-marker"
    );
    for key in ["credential_ref", "ssh_credential_ref", "tls_credential_ref"] {
        assert_ne!(json[key].as_str(), Some(reference));
    }
    assert_eq!(vault.items.lock().unwrap().len(), 4);
    for file in std::fs::read_dir(directory.path()).unwrap() {
        let bytes = std::fs::read(file.unwrap().path()).unwrap();
        assert!(
            !bytes
                .windows(19)
                .any(|bytes| bytes == b"proxy-secret-marker")
        );
    }
}

fn save(
    engine: &mut Engine,
    profile: ConnectionProfile,
    update: CredentialUpdate,
    token: u64,
) -> ConnectionProfile {
    engine
        .profile_save_with_secrets(profile, updates(update), token)
        .unwrap();
    let Event::ProfileSaved { profile, .. } = event(engine) else {
        panic!("proxy credential save failed")
    };
    *profile
}
#[test]
fn proxy_credentials_keep_replace_clear_duplicate_and_delete_without_sharing_ownership() {
    let vault = Arc::new(Vault::default());
    let mut engine =
        Engine::new_with_credentials(EngineConfig::default(), vec![], vault.clone()).unwrap();
    let mut original = save(
        &mut engine,
        profile(),
        CredentialUpdate::Replace(Secret::new("original")),
        1,
    );
    let first = original.proxy_credential_ref.clone().unwrap();
    original.proxy_credential_ref = Some("unowned-reference".into());
    original = save(&mut engine, original, CredentialUpdate::Keep, 2);
    assert_eq!(original.proxy_credential_ref.as_ref(), Some(&first));
    original = save(
        &mut engine,
        original,
        CredentialUpdate::Replace(Secret::new("replacement")),
        3,
    );
    let second = original.proxy_credential_ref.clone().unwrap();
    assert_ne!(first, second);
    assert!(matches!(vault.get(&first), Err(CredentialError::Missing)));
    assert_eq!(vault.get(&second).unwrap().expose(), "replacement");
    engine
        .profile_duplicate("proxy".into(), "copy".into(), "Copy".into(), 4)
        .unwrap();
    let Event::ProfileSaved { profile: copy, .. } = event(&mut engine) else {
        panic!("duplicate failed")
    };
    assert!(copy.proxy_credential_ref.is_none());
    let copy = save(
        &mut engine,
        *copy,
        CredentialUpdate::Replace(Secret::new("copy-password")),
        5,
    );
    let third = copy.proxy_credential_ref.clone().unwrap();
    let copy = save(&mut engine, copy, CredentialUpdate::Clear, 6);
    assert!(copy.proxy_credential_ref.is_none());
    assert!(matches!(vault.get(&third), Err(CredentialError::Missing)));
    assert_eq!(vault.get(&second).unwrap().expose(), "replacement");
    engine.profile_delete("proxy".into(), 7).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileDeleted { .. }));
    assert!(matches!(vault.get(&second), Err(CredentialError::Missing)));
    assert!(vault.items.lock().unwrap().is_empty());
}

struct Driver {
    id: &'static str,
    seen: Arc<Mutex<Vec<Option<String>>>>,
}
#[async_trait::async_trait]
impl choscordb_driver_api::DatabaseDriver for Driver {
    fn id(&self) -> &'static str {
        self.id
    }
    fn capabilities(&self) -> choscordb_driver_api::DriverCapabilities {
        Default::default()
    }
    async fn connect(
        &self,
        options: choscordb_driver_api::ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn choscordb_driver_api::Connection>> {
        use choscordb_driver_api::*;
        let secret = match options {
            ConnectionOptions::Postgres { proxy_secret, .. }
            | ConnectionOptions::Mysql { proxy_secret, .. } => proxy_secret,
            ConnectionOptions::Sqlite { .. } => None,
        };
        self.seen
            .lock()
            .unwrap()
            .push(secret.map(|s| s.expose().to_owned()));
        choscordb_driver_sqlite::SqliteDriver
            .connect(ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            })
            .await
    }
}
fn network_profile(driver: &str, proxy: serde_json::Value) -> ConnectionProfile {
    serde_json::from_value(serde_json::json!({"id":"proxy", "name":"Proxy", "group_id":null,"credential_ref":null,"proxy_credential_ref":"saved-proxy",
        "configuration":{"driver":driver,"host":"localhost","port":5432,"database":"db","user":"tester","tls":{"mode":"Disable","root_certificate_path":null},"proxy":proxy}})).unwrap()
}
#[test]
fn proxy_password_resolution_and_transient_empty_override_match_test_and_connect() {
    use choscordb_core::ProfileSecrets;
    for driver in ["postgres", "mysql"] {
        let vault = Arc::new(Vault::default());
        vault
            .put("saved-proxy", &Secret::new("stored-proxy"))
            .unwrap();
        let seen = Arc::new(Mutex::new(Vec::new()));
        let mut engine = Engine::new_with_credentials(
            EngineConfig::default(),
            vec![Arc::new(Driver {
                id: driver,
                seen: seen.clone(),
            })],
            vault,
        )
        .unwrap();
        let profile = network_profile(
            driver,
            serde_json::json!({"protocol":"socks5","host":"localhost","port":1080,"username":"proxy-user"}),
        );
        engine.test_profile(profile.clone(), None, 1).unwrap();
        assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
        engine.connect_profile(profile.clone(), None).unwrap();
        assert!(matches!(event(&mut engine), Event::Connected { .. }));
        engine
            .test_profile_with_secrets(
                profile.clone(),
                ProfileSecrets {
                    ssh_private_key: None,
                    ssh_jump_private_keys: Default::default(),
                    ssh_jumps: Default::default(),
                    proxy: Some(Secret::new("once")),
                    ..Default::default()
                },
                2,
            )
            .unwrap();
        assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
        engine
            .connect_profile_with_secrets(
                profile,
                ProfileSecrets {
                    ssh_private_key: None,
                    ssh_jump_private_keys: Default::default(),
                    ssh_jumps: Default::default(),
                    proxy: Some(Secret::new("")),
                    ..Default::default()
                },
            )
            .unwrap();
        assert!(matches!(event(&mut engine), Event::Connected { .. }));
        assert_eq!(
            *seen.lock().unwrap(),
            vec![
                Some("stored-proxy".into()),
                Some("stored-proxy".into()),
                Some("once".into()),
                Some("".into())
            ]
        );
    }
}

#[test]
fn unused_proxy_credentials_never_block_sqlite_disabled_socks4_or_anonymous_socks5() {
    use choscordb_core::ProfileSecrets;
    for driver in ["sqlite", "postgres", "mysql"] {
        for proxy in [
            serde_json::Value::Null,
            serde_json::json!({"protocol":"socks4","host":"localhost","username":"socks4-user"}),
            serde_json::json!({"protocol":"socks5","host":"localhost"}),
        ] {
            let vault = Arc::new(Vault::default());
            let seen = Arc::new(Mutex::new(Vec::new()));
            let mut engine = Engine::new_with_credentials(
                EngineConfig::default(),
                vec![Arc::new(Driver {
                    id: driver,
                    seen: seen.clone(),
                })],
                vault,
            )
            .unwrap();
            let mut profile = if driver == "sqlite" {
                profile()
            } else {
                network_profile(driver, proxy)
            };
            profile.proxy_credential_ref = Some("missing-unused-proxy".into());
            engine
                .test_profile_with_secrets(
                    profile.clone(),
                    ProfileSecrets {
                        ssh_private_key: None,
                        ssh_jump_private_keys: Default::default(),
                        ssh_jumps: Default::default(),
                        proxy: Some(Secret::new("unused-transient")),
                        ..Default::default()
                    },
                    1,
                )
                .unwrap();
            assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
            engine.connect_profile(profile, None).unwrap();
            assert!(matches!(event(&mut engine), Event::Connected { .. }));
            assert_eq!(*seen.lock().unwrap(), vec![None, None]);
        }
    }
}
#[test]
fn authenticated_socks5_still_fails_for_a_missing_saved_password() {
    for driver in ["postgres", "mysql"] {
        let vault = Arc::new(Vault::default());
        let seen = Arc::new(Mutex::new(Vec::new()));
        let mut engine = Engine::new_with_credentials(
            EngineConfig::default(),
            vec![Arc::new(Driver {
                id: driver,
                seen: seen.clone(),
            })],
            vault,
        )
        .unwrap();
        let profile = network_profile(
            driver,
            serde_json::json!({"protocol":"socks5","host":"localhost","username":"proxy-user"}),
        );
        engine.test_profile(profile.clone(), None, 1).unwrap();
        assert!(
            matches!(event(&mut engine),Event::ProfileFailed{error,..} if error.kind==choscordb_driver_api::ErrorKind::Io)
        );
        engine.connect_profile(profile, None).unwrap();
        assert!(
            matches!(event(&mut engine),Event::ConnectionFailed{error,..} if error.kind==choscordb_driver_api::ErrorKind::Io)
        );
        assert!(seen.lock().unwrap().is_empty());
    }
}

#[test]
fn active_socks5_password_replacements_are_validated_before_writing_metadata_or_vault() {
    let vault = Arc::new(Vault::default());
    let mut engine =
        Engine::new_with_credentials(EngineConfig::default(), vec![], vault.clone()).unwrap();
    let original = save(
        &mut engine,
        network_profile(
            "postgres",
            serde_json::json!({"protocol":"socks5","host":"localhost","username":"proxy-user"}),
        ),
        CredentialUpdate::Replace(Secret::new("valid-proxy")),
        1,
    );
    let reference = original.proxy_credential_ref.clone().unwrap();
    for invalid in [String::new(), "é".repeat(128)] {
        assert_eq!(
            engine.profile_save_with_secrets(
                original.clone(),
                updates(CredentialUpdate::Replace(Secret::new(invalid))),
                2
            ),
            Err(choscordb_core::SubmitError::InvalidInput)
        );
        assert_eq!(vault.items.lock().unwrap().len(), 1);
        assert_eq!(vault.get(&reference).unwrap().expose(), "valid-proxy");
        engine.profile_list(3).unwrap();
        let Event::Profiles { profiles, .. } = event(&mut engine) else {
            panic!("list failed")
        };
        assert_eq!(profiles, vec![original.clone()]);
    }
}

#[test]
fn failed_proxy_metadata_publication_removes_new_credentials_and_preserves_the_old_reference() {
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
    let original = save(
        &mut engine,
        profile(),
        CredentialUpdate::Replace(Secret::new("old-proxy")),
        1,
    );
    let reference = original.proxy_credential_ref.clone().unwrap();
    let store = rusqlite::Connection::open(&metadata).unwrap();
    store.execute_batch("CREATE TRIGGER fail_proxy_update BEFORE UPDATE ON connection_profiles BEGIN SELECT RAISE(ABORT,'fixture failure'); END;").unwrap();
    engine
        .profile_save_with_secrets(
            original.clone(),
            updates(CredentialUpdate::Replace(Secret::new("new-proxy"))),
            2,
        )
        .unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileFailed { .. }));
    assert_eq!(vault.items.lock().unwrap().len(), 1);
    assert_eq!(vault.get(&reference).unwrap().expose(), "old-proxy");
    assert_eq!(
        choscordb_storage::Storage::open(&metadata)
            .unwrap()
            .profile("proxy")
            .unwrap(),
        Some(original)
    );
}

#[test]
fn proxy_vault_failure_rolls_back_all_new_credential_writes() {
    let vault = Arc::new(Vault::default());
    let mut engine =
        Engine::new_with_credentials(EngineConfig::default(), vec![], vault.clone()).unwrap();
    let original = save(
        &mut engine,
        profile(),
        CredentialUpdate::Replace(Secret::new("old-proxy")),
        1,
    );
    let before = vault.items.lock().unwrap().clone();
    *vault.reject.lock().unwrap() = Some("rejected-proxy".into());
    engine
        .profile_save_with_secrets(
            original.clone(),
            CredentialUpdates {
                ssh_private_key: CredentialUpdate::Keep,
                ssh_jump_private_keys: Default::default(),
                ssh_jumps: Default::default(),
                database: CredentialUpdate::Replace(Secret::new("new-database")),
                ssh: CredentialUpdate::Replace(Secret::new("new-ssh")),
                tls: CredentialUpdate::Replace(Secret::new("new-tls")),
                proxy: CredentialUpdate::Replace(Secret::new("rejected-proxy")),
            },
            2,
        )
        .unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileFailed { .. }));
    assert_eq!(*vault.items.lock().unwrap(), before);
    engine.profile_list(3).unwrap();
    let Event::Profiles { profiles, .. } = event(&mut engine) else {
        panic!("list failed")
    };
    assert_eq!(profiles, vec![original]);
}
