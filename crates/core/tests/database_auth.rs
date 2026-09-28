use choscordb_core::{ConnectionProfile, Engine, EngineConfig, Event};
use choscordb_credentials::{CredentialError, CredentialStore};
use choscordb_driver_api::{
    Connection, ConnectionOptions, DatabaseDriver, DriverCapabilities, Result, Secret,
};
use std::{
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};

struct Driver {
    id: &'static str,
    observed: Arc<Mutex<Vec<Option<String>>>>,
}
#[async_trait::async_trait]
impl DatabaseDriver for Driver {
    fn id(&self) -> &'static str {
        self.id
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>> {
        let password = match options {
            ConnectionOptions::Postgres { password, .. }
            | ConnectionOptions::Mysql { password, .. } => password,
            _ => panic!("network options expected"),
        };
        self.observed
            .lock()
            .unwrap()
            .push(password.map(|secret| secret.expose().to_owned()));
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

#[test]
fn passwordless_and_transient_password_reach_driver_without_profile_metadata() {
    let profile: ConnectionProfile = serde_json::from_value(serde_json::json!({
        "id":"auth", "name":"Password optional", "credential_ref":null,
        "configuration":{"driver":"postgres", "host":"localhost", "port":5432,
            "database":"db", "user":"alice", "tls":{"mode":"Disable"}}
    }))
    .unwrap();
    let observed = Arc::new(Mutex::new(Vec::new()));
    let mut engine = Engine::new(
        EngineConfig::default(),
        vec![Arc::new(Driver {
            id: "postgres",
            observed: observed.clone(),
        })],
    )
    .unwrap();
    engine.test_profile(profile.clone(), None, 9).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
    engine
        .connect_profile(profile.clone(), Some(Secret::new("entered-only")))
        .unwrap();
    assert!(matches!(event(&mut engine), Event::Connected { .. }));
    assert_eq!(
        *observed.lock().unwrap(),
        vec![None, Some("entered-only".into())]
    );
    let encoded = serde_json::to_value(profile).unwrap();
    assert!(encoded.get("authentication").is_none());
    assert!(!encoded.to_string().contains("entered-only"));
}

struct SavedPassword;
impl CredentialStore for SavedPassword {
    fn get(&self, reference: &str) -> choscordb_credentials::Result<Secret> {
        if reference == "saved-ref" {
            Ok(Secret::new("saved-password"))
        } else {
            Err(CredentialError::Missing)
        }
    }
    fn put(&self, _: &str, _: &Secret) -> choscordb_credentials::Result<()> {
        panic!("connection must not save a credential")
    }
    fn delete(&self, _: &str) -> choscordb_credentials::Result<()> {
        panic!("connection must not delete a credential")
    }
}

#[test]
fn empty_transient_password_suppresses_saved_reference_and_reaches_driver_as_none() {
    for (driver, port) in [("postgres", 5432), ("mysql", 3306)] {
        let profile: ConnectionProfile = serde_json::from_value(serde_json::json!({
            "id":"auth", "name":"Password optional", "credential_ref":"saved-ref",
            "configuration":{"driver":driver, "host":"localhost", "port":port,
                "database":"db", "user":"alice", "tls":{"mode":"Disable"}}
        }))
        .unwrap();
        let observed = Arc::new(Mutex::new(Vec::new()));
        let mut engine = Engine::new_with_credentials(
            EngineConfig::default(),
            vec![Arc::new(Driver {
                id: driver,
                observed: observed.clone(),
            })],
            Arc::new(SavedPassword),
        )
        .unwrap();
        engine.test_profile(profile.clone(), None, 10).unwrap();
        assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
        engine
            .test_profile(profile.clone(), Some(Secret::new("")), 11)
            .unwrap();
        assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
        engine
            .connect_profile(profile.clone(), Some(Secret::new("")))
            .unwrap();
        assert!(matches!(event(&mut engine), Event::Connected { .. }));
        engine
            .connect_profile(profile, Some(Secret::new("typed-password")))
            .unwrap();
        assert!(matches!(event(&mut engine), Event::Connected { .. }));
        assert_eq!(
            *observed.lock().unwrap(),
            vec![
                Some("saved-password".into()),
                None,
                None,
                Some("typed-password".into()),
            ],
            "{driver}"
        );
    }
}
