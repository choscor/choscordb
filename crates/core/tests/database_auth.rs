use choscordb_core::{ConnectionProfile, Engine, EngineConfig, Event, ProfileSecrets};
use choscordb_credentials::{CredentialError, CredentialStore};
use choscordb_driver_api::*;
use std::{
    sync::{
        Arc, Mutex,
        atomic::{AtomicUsize, Ordering},
    },
    time::{Duration, Instant},
};
#[derive(Default)]
struct Vault(AtomicUsize);
impl CredentialStore for Vault {
    fn get(&self, _: &str) -> choscordb_credentials::Result<Secret> {
        self.0.fetch_add(1, Ordering::SeqCst);
        Err(CredentialError::Missing)
    }
    fn put(&self, _: &str, _: &Secret) -> choscordb_credentials::Result<()> {
        panic!("provider passwords must not be saved")
    }
    fn delete(&self, _: &str) -> choscordb_credentials::Result<()> {
        Ok(())
    }
}
struct Driver(Arc<Mutex<Vec<(String, String)>>>);
#[async_trait::async_trait]
impl DatabaseDriver for Driver {
    fn id(&self) -> &'static str {
        "postgres"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>> {
        let ConnectionOptions::Postgres { user, password, .. } = options else {
            panic!("PG")
        };
        self.0
            .lock()
            .unwrap()
            .push((user, password.expect("provider password").expose().into()));
        choscordb_driver_sqlite::SqliteDriver
            .connect(ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            })
            .await
    }
}
fn event(engine: &mut Engine) -> Event {
    let deadline = Instant::now() + Duration::from_secs(4);
    loop {
        if let Some(event) = engine.try_event() {
            return event;
        }
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(1));
    }
}
#[cfg(unix)]
#[test]
fn test_and_connect_refresh_command_password_without_accessing_saved_secret() {
    let directory = tempfile::tempdir().unwrap();
    std::fs::write(directory.path().join("credential"), "first\n").unwrap();
    let profile:ConnectionProfile=serde_json::from_value(serde_json::json!({
        "id":"auth","name":"Command auth","group_id":null,"credential_ref":"obsolete-ref",
        "authentication":{"method":"command","command":"cat credential","working_directory":directory.path().to_str().unwrap(),"timeout_seconds":2},
        "configuration":{"driver":"postgres","host":"localhost","port":5432,"database":"db","user":"alice","tls":{"mode":"Disable","root_certificate_path":null}}
    })).unwrap();
    let observed = Arc::new(Mutex::new(Vec::new()));
    let vault = Arc::new(Vault::default());
    let mut engine = Engine::new_with_credentials(
        EngineConfig::default(),
        vec![Arc::new(Driver(observed.clone()))],
        vault.clone(),
    )
    .unwrap();
    engine.test_profile(profile.clone(), None, 9).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
    std::fs::write(directory.path().join("credential"), "second\n").unwrap();
    engine.connect_profile(profile.clone(), None).unwrap();
    assert!(matches!(event(&mut engine), Event::Connected { .. }));
    std::fs::remove_file(directory.path().join("credential")).unwrap();
    engine
        .connect_profile_with_secrets(
            profile,
            ProfileSecrets {
                ssh_private_key: None,
                ssh_jump_private_keys: Default::default(),
                ssh_jumps: Default::default(),
                database: Some(Secret::new("")),
                ..Default::default()
            },
        )
        .unwrap();
    assert!(matches!(event(&mut engine), Event::Connected { .. }));
    assert_eq!(
        *observed.lock().unwrap(),
        vec![
            ("alice".into(), "first".into()),
            ("alice".into(), "second".into()),
            ("alice".into(), "".into())
        ]
    );
    assert_eq!(vault.0.load(Ordering::SeqCst), 0);
}

#[test]
fn passfile_authentication_fills_username_and_bypasses_saved_credential_reference() {
    use std::io::Write;
    let mut file = tempfile::NamedTempFile::new().unwrap();
    writeln!(file, "localhost:5432:alice:alice:from-passfile").unwrap();
    let profile:ConnectionProfile=serde_json::from_value(serde_json::json!({
        "id":"pgpass","name":"Passfile auth","group_id":null,"credential_ref":"obsolete-ref",
        "authentication":{"method":"pg_pass","path":file.path().to_str().unwrap()},
        "configuration":{"driver":"postgres","host":"/tmp/pgsocket","port":5432,"database":"","user":"","tls":{"mode":"Disable","root_certificate_path":null}}
    })).unwrap();
    let observed = Arc::new(Mutex::new(Vec::new()));
    let vault = Arc::new(Vault::default());
    let mut engine = Engine::new_with_credentials(
        EngineConfig::default(),
        vec![Arc::new(Driver(observed.clone()))],
        vault.clone(),
    )
    .unwrap();
    engine.test_profile(profile.clone(), None, 10).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
    engine.connect_profile(profile, None).unwrap();
    assert!(matches!(event(&mut engine), Event::Connected { .. }));
    assert_eq!(
        *observed.lock().unwrap(),
        vec![
            ("alice".into(), "from-passfile".into()),
            ("alice".into(), "from-passfile".into())
        ]
    );
    assert_eq!(vault.0.load(Ordering::SeqCst), 0);
}

#[cfg(unix)]
#[test]
fn command_provider_completes_within_its_timeout() {
    let profile:ConnectionProfile=serde_json::from_value(serde_json::json!({
        "id":"slow-provider","name":"Slow provider","group_id":null,"credential_ref":null,
        "authentication":{"method":"command","command":"sleep 7; printf delayed-password","timeout_seconds":10},
        "configuration":{"driver":"postgres","host":"localhost","port":5432,"database":"db","user":"alice",
            "tls":{"mode":"Disable","root_certificate_path":null}}
    })).unwrap();
    let observed = Arc::new(Mutex::new(Vec::new()));
    let mut engine = Engine::new_with_credentials(
        EngineConfig::default(),
        vec![Arc::new(Driver(observed.clone()))],
        Arc::new(Vault::default()),
    )
    .unwrap();
    engine.test_profile(profile, None, 11).unwrap();
    let deadline = Instant::now() + Duration::from_secs(12);
    let result = loop {
        if let Some(event) = engine.try_event() {
            break event;
        }
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(5));
    };
    assert!(
        matches!(result, Event::ProfileTested { .. }),
        "command provider should complete within its timeout"
    );
    assert_eq!(
        *observed.lock().unwrap(),
        vec![("alice".into(), "delayed-password".into())]
    );
}
