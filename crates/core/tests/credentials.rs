use choscordb_core::{
    ConnectionProfile, CredentialUpdate, Engine, EngineConfig, Event, PostgresTls,
    ProfileConfiguration, SubmitError,
};
use choscordb_credentials::{CredentialError, CredentialStore, Secret};
use choscordb_driver_api::{Connection, ConnectionOptions, DatabaseDriver, DriverCapabilities};
use std::{
    collections::HashMap,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
    time::{Duration, Instant},
};
#[derive(Default)]
struct Vault {
    items: Mutex<HashMap<String, String>>,
    fail_put: AtomicBool,
    fail_delete: AtomicBool,
}
impl CredentialStore for Vault {
    fn get(&self, r: &str) -> choscordb_credentials::Result<Secret> {
        self.items
            .lock()
            .unwrap()
            .get(r)
            .cloned()
            .map(Secret::new)
            .ok_or(CredentialError::Missing)
    }
    fn put(&self, r: &str, s: &Secret) -> choscordb_credentials::Result<()> {
        if self.fail_put.load(Ordering::SeqCst) {
            return Err(CredentialError::Unavailable);
        }
        self.items
            .lock()
            .unwrap()
            .insert(r.into(), s.expose().into());
        Ok(())
    }
    fn delete(&self, r: &str) -> choscordb_credentials::Result<()> {
        if self.fail_delete.load(Ordering::SeqCst) {
            return Err(CredentialError::Unavailable);
        }
        self.items.lock().unwrap().remove(r);
        Ok(())
    }
}
fn profile() -> ConnectionProfile {
    ConnectionProfile {
        id: "p".into(),
        name: "Remote".into(),
        group_id: None,
        configuration: ProfileConfiguration::Postgres {
            ssh: None,
            host: "localhost".into(),
            port: 5432,
            database: "db".into(),
            user: "u".into(),
            tls: PostgresTls::default(),
        },
        credential_ref: None,
    }
}
fn event(engine: &mut Engine) -> Event {
    let until = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(e) = engine.try_event() {
            return e;
        }
        assert!(Instant::now() < until);
        std::thread::sleep(Duration::from_millis(2));
    }
}
fn save(engine: &mut Engine, p: ConnectionProfile, s: &str) -> ConnectionProfile {
    engine
        .profile_save_with_secret(p, CredentialUpdate::Replace(Secret::new(s)), 1)
        .unwrap();
    match event(engine) {
        Event::ProfileSaved { profile, .. } => profile,
        e => panic!("{e:?}"),
    }
}
#[test]
fn replacement_is_copy_on_write_and_keep_cannot_forge_references() {
    let vault = Arc::new(Vault::default());
    let mut engine =
        Engine::new_with_credentials(EngineConfig::default(), vec![], vault.clone()).unwrap();
    let first = save(&mut engine, profile(), "original-private-password");
    vault.fail_put.store(true, Ordering::SeqCst);
    engine
        .profile_save_with_secret(
            first.clone(),
            CredentialUpdate::Replace(Secret::new("replacement")),
            2,
        )
        .unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileFailed { .. }));
    assert_eq!(
        vault
            .get(first.credential_ref.as_ref().unwrap())
            .unwrap()
            .expose(),
        "original-private-password"
    );
    vault.fail_put.store(false, Ordering::SeqCst);
    let mut forged = first.clone();
    forged.credential_ref = Some("foreign".into());
    engine.profile_save(forged, 3).unwrap();
    assert!(
        matches!(event(&mut engine),Event::ProfileSaved{profile,..} if profile.credential_ref==first.credential_ref)
    );
    let second = save(&mut engine, first.clone(), "new-private-password");
    assert_ne!(first.credential_ref, second.credential_ref);
    assert_eq!(vault.items.lock().unwrap().len(), 1);
    engine
        .profile_save_with_secret(second, CredentialUpdate::Clear, 4)
        .unwrap();
    assert!(
        matches!(event(&mut engine),Event::ProfileSaved{profile,..} if profile.credential_ref.is_none())
    );
    assert!(vault.items.lock().unwrap().is_empty());
}
#[test]
fn cleanup_failures_are_committed_and_recovered_after_restart() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("metadata.sqlite");
    let config = EngineConfig {
        storage_path: Some(path.clone()),
        ..Default::default()
    };
    let vault = Arc::new(Vault::default());
    let mut engine = Engine::new_with_credentials(config.clone(), vec![], vault.clone()).unwrap();
    let first = save(&mut engine, profile(), "secret-marker-never-in-metadata");
    vault.fail_delete.store(true, Ordering::SeqCst);
    let second = save(&mut engine, first, "replacement-marker-never-in-metadata");
    engine.profile_delete(second.id, 4).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::ProfileDeleted {
            warning: Some(_),
            ..
        }
    ));
    let store = choscordb_storage::Storage::open(&path).unwrap();
    assert!(store.profiles().unwrap().is_empty());
    assert_eq!(store.pending_credential_cleanup().unwrap().len(), 2);
    let bytes = std::fs::read(&path).unwrap();
    for marker in [
        b"secret-marker-never-in-metadata".as_slice(),
        b"replacement-marker-never-in-metadata".as_slice(),
    ] {
        assert!(!bytes.windows(marker.len()).any(|b| b == marker));
    }
    drop(engine);
    vault.fail_delete.store(false, Ordering::SeqCst);
    let mut engine = Engine::new_with_credentials(config, vec![], vault.clone()).unwrap();
    engine.profile_list(5).unwrap();
    assert!(matches!(event(&mut engine), Event::Profiles { .. }));
    assert!(vault.items.lock().unwrap().is_empty());
    assert!(store.pending_credential_cleanup().unwrap().is_empty());
}
#[test]
fn failed_metadata_publication_removes_new_key_and_preserves_old() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("meta.sqlite");
    let vault = Arc::new(Vault::default());
    let mut engine = Engine::new_with_credentials(
        EngineConfig {
            storage_path: Some(path.clone()),
            ..Default::default()
        },
        vec![],
        vault.clone(),
    )
    .unwrap();
    let first = save(&mut engine, profile(), "old");
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute_batch("CREATE TRIGGER deny_save BEFORE UPDATE ON connection_profiles BEGIN SELECT RAISE(ABORT,'injected'); END;").unwrap();
    engine
        .profile_save_with_secret(
            first.clone(),
            CredentialUpdate::Replace(Secret::new("new")),
            2,
        )
        .unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileFailed { .. }));
    assert_eq!(vault.items.lock().unwrap().len(), 1);
    assert_eq!(
        vault
            .get(first.credential_ref.as_ref().unwrap())
            .unwrap()
            .expose(),
        "old"
    );
    assert_eq!(
        choscordb_storage::Storage::open(path)
            .unwrap()
            .profile("p")
            .unwrap(),
        Some(first)
    );
}
struct PasswordDriver {
    received: Arc<Mutex<Vec<String>>>,
}
#[async_trait::async_trait]
impl DatabaseDriver for PasswordDriver {
    fn id(&self) -> &'static str {
        "postgres"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(
        &self,
        options: ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn Connection>> {
        let ConnectionOptions::Postgres { password, .. } = options else {
            panic!()
        };
        self.received
            .lock()
            .unwrap()
            .push(password.map(|p| p.expose().to_owned()).unwrap_or_default());
        choscordb_driver_sqlite::SqliteDriver
            .connect(ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            })
            .await
    }
}
#[test]
fn connect_and_test_resolve_credentials_without_persisting_overrides() {
    let vault = Arc::new(Vault::default());
    let received = Arc::new(Mutex::new(vec![]));
    let mut engine = Engine::new_with_credentials(
        EngineConfig::default(),
        vec![Arc::new(PasswordDriver {
            received: received.clone(),
        })],
        vault.clone(),
    )
    .unwrap();
    let p = save(&mut engine, profile(), "saved");
    engine.test_profile(p.clone(), None, 2).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
    engine.connect_profile(p.clone(), None).unwrap();
    assert!(matches!(event(&mut engine), Event::Connected { .. }));
    engine
        .test_profile(p.clone(), Some(Secret::new("once")), 3)
        .unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
    assert_eq!(*received.lock().unwrap(), vec!["saved", "saved", "once"]);
    vault.items.lock().unwrap().clear();
    engine.test_profile(p.clone(), None, 4).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileFailed { .. }));
    engine.connect_profile(p, None).unwrap();
    assert!(matches!(event(&mut engine), Event::ConnectionFailed { .. }));
    assert_eq!(received.lock().unwrap().len(), 3);
    assert_eq!(
        engine.test_profile(profile(), Some(Secret::new("x".repeat(16385))), 5),
        Err(SubmitError::ResourceLimit)
    );
}
#[test]
fn cleanup_never_deletes_shared_profile_reference() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("meta.sqlite");
    let vault = Arc::new(Vault::default());
    let mut store = choscordb_storage::Storage::open(&path).unwrap();
    let mut p = profile();
    p.credential_ref = Some("shared".into());
    store.save_profile(&p).unwrap();
    p.id = "other".into();
    store.save_profile(&p).unwrap();
    vault
        .put("shared", &Secret::new("shared-password"))
        .unwrap();
    let mut engine = Engine::new_with_credentials(
        EngineConfig {
            storage_path: Some(path),
            ..Default::default()
        },
        vec![],
        vault.clone(),
    )
    .unwrap();
    engine.profile_delete("p".into(), 1).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileDeleted { .. }));
    assert!(vault.get("shared").is_ok());
    engine.profile_delete("other".into(), 2).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileDeleted { .. }));
    assert!(vault.get("shared").is_err());
}

#[derive(Default)]
struct BlockingVault {
    entered: std::sync::atomic::AtomicUsize,
    released: Mutex<bool>,
    changed: std::sync::Condvar,
}
impl CredentialStore for BlockingVault {
    fn get(&self, _: &str) -> choscordb_credentials::Result<Secret> {
        self.entered.fetch_add(1, Ordering::SeqCst);
        let mut released = self.released.lock().unwrap();
        while !*released {
            released = self.changed.wait(released).unwrap();
        }
        Ok(Secret::new("resolved"))
    }
    fn put(&self, _: &str, _: &Secret) -> choscordb_credentials::Result<()> {
        Ok(())
    }
    fn delete(&self, _: &str) -> choscordb_credentials::Result<()> {
        Ok(())
    }
}
#[test]
fn blocked_os_prompt_keeps_submission_bounded_and_shutdown_nonblocking() {
    let vault = Arc::new(BlockingVault::default());
    let received = Arc::new(Mutex::new(vec![]));
    let mut engine = Engine::new_with_credentials(
        EngineConfig {
            command_capacity: 1,
            ..Default::default()
        },
        vec![Arc::new(PasswordDriver { received })],
        vault.clone(),
    )
    .unwrap();
    let mut p = profile();
    p.credential_ref = Some("blocked".into());
    engine.connect_profile(p.clone(), None).unwrap();
    let until = Instant::now() + Duration::from_secs(5);
    while vault.entered.load(Ordering::SeqCst) == 0 {
        assert!(Instant::now() < until);
        std::thread::sleep(Duration::from_millis(1));
    }
    for _ in 0..5 {
        engine.connect_profile(p.clone(), None).unwrap();
    }
    // A saturated worker queue reports bounded failure; it never starts more OS calls.
    loop {
        if matches!(event(&mut engine), Event::ConnectionFailed { .. }) {
            break;
        }
    }
    assert_eq!(vault.entered.load(Ordering::SeqCst), 1);
    let started = Instant::now();
    engine.initiate_shutdown();
    assert_eq!(engine.profile_list(10), Err(SubmitError::ShuttingDown));
    drop(engine);
    assert!(started.elapsed() < Duration::from_secs(1));
    *vault.released.lock().unwrap() = true;
    vault.changed.notify_all();
}

#[derive(Default)]
struct PublishingVault {
    inner: Vault,
    entered: AtomicBool,
    release: Mutex<bool>,
    changed: std::sync::Condvar,
}
impl CredentialStore for PublishingVault {
    fn get(&self, r: &str) -> choscordb_credentials::Result<Secret> {
        self.inner.get(r)
    }
    fn delete(&self, r: &str) -> choscordb_credentials::Result<()> {
        self.inner.delete(r)
    }
    fn put(&self, r: &str, s: &Secret) -> choscordb_credentials::Result<()> {
        self.inner.put(r, s)?;
        self.entered.store(true, Ordering::SeqCst);
        let mut release = self.release.lock().unwrap();
        while !*release {
            release = self.changed.wait(release).unwrap();
        }
        Ok(())
    }
}
#[test]
fn another_engine_cannot_cleanup_an_inflight_publication() {
    let dir = tempfile::tempdir().unwrap();
    let config = EngineConfig {
        storage_path: Some(dir.path().join("meta.sqlite")),
        ..Default::default()
    };
    let vault = Arc::new(PublishingVault::default());
    let mut first = Engine::new_with_credentials(config.clone(), vec![], vault.clone()).unwrap();
    first
        .profile_save_with_secret(
            profile(),
            CredentialUpdate::Replace(Secret::new("protected")),
            1,
        )
        .unwrap();
    let until = Instant::now() + Duration::from_secs(5);
    while !vault.entered.load(Ordering::SeqCst) {
        assert!(Instant::now() < until);
        std::thread::sleep(Duration::from_millis(1));
    }
    let mut second = Engine::new_with_credentials(config, vec![], vault.clone()).unwrap();
    second.profile_list(2).unwrap();
    std::thread::sleep(Duration::from_millis(100));
    let premature = second.try_event();
    *vault.release.lock().unwrap() = true;
    vault.changed.notify_all();
    assert!(
        premature.is_none(),
        "another metadata worker bypassed publication lock: {premature:?}"
    );
    let Event::ProfileSaved { profile, .. } = event(&mut first) else {
        panic!()
    };
    assert!(matches!(event(&mut second), Event::Profiles { .. }));
    assert_eq!(
        vault
            .get(profile.credential_ref.as_ref().unwrap())
            .unwrap()
            .expose(),
        "protected"
    );
}

struct MysqlPasswordDriver(Arc<Mutex<Vec<String>>>);
#[async_trait::async_trait]
impl DatabaseDriver for MysqlPasswordDriver {
    fn id(&self) -> &'static str {
        "mysql"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(
        &self,
        options: ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn Connection>> {
        let ConnectionOptions::Mysql { password, .. } = options else {
            panic!("wrong options")
        };
        self.0
            .lock()
            .unwrap()
            .push(password.map(|p| p.expose().to_owned()).unwrap_or_default());
        choscordb_driver_sqlite::SqliteDriver
            .connect(ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            })
            .await
    }
}
#[test]
fn mysql_profiles_resolve_saved_credentials_and_allow_transient_override() {
    let received = Arc::new(Mutex::new(vec![]));
    let mut engine = Engine::new_with_credentials(
        EngineConfig::default(),
        vec![Arc::new(MysqlPasswordDriver(received.clone()))],
        Arc::new(Vault::default()),
    )
    .unwrap();
    let mut p = profile();
    p.configuration = ProfileConfiguration::Mysql {
        ssh: None,
        host: "db.example".into(),
        port: 3306,
        database: "inventory".into(),
        user: "reader".into(),
        tls: PostgresTls::default(),
    };
    let p = save(&mut engine, p, "saved-mysql");
    engine.test_profile(p.clone(), None, 2).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
    engine.connect_profile(p.clone(), None).unwrap();
    assert!(matches!(event(&mut engine), Event::Connected { .. }));
    engine
        .connect_profile(p, Some(Secret::new("once-mysql")))
        .unwrap();
    assert!(matches!(event(&mut engine), Event::Connected { .. }));
    assert_eq!(
        *received.lock().unwrap(),
        vec!["saved-mysql", "saved-mysql", "once-mysql"]
    );
}
