use choscordb_core::{
    ConnectionProfile, Engine, EngineConfig, Event, PostgresTls, ProfileConfiguration,
};
use choscordb_credentials::{CredentialError, CredentialStore, Secret};
use choscordb_driver_api::{
    Connection, ConnectionOptions, DatabaseDriver, DriverCapabilities, SshAuthentication, SshTunnel,
};
use std::{
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};

#[derive(Default)]
struct Vault(Mutex<Vec<String>>);
impl CredentialStore for Vault {
    fn get(&self, reference: &str) -> choscordb_credentials::Result<Secret> {
        self.0.lock().unwrap().push(reference.into());
        Err(CredentialError::Missing)
    }
    fn put(&self, _: &str, _: &Secret) -> choscordb_credentials::Result<()> {
        unreachable!()
    }
    fn delete(&self, _: &str) -> choscordb_credentials::Result<()> {
        Ok(())
    }
}
struct Driver(&'static str);
#[async_trait::async_trait]
impl DatabaseDriver for Driver {
    fn id(&self) -> &'static str {
        self.0
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(
        &self,
        options: ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn Connection>> {
        match options {
            ConnectionOptions::Mysql { ssh_secret, .. }
            | ConnectionOptions::Postgres { ssh_secret, .. } => assert!(ssh_secret.is_none()),
            ConnectionOptions::Sqlite { .. } => (),
        }
        choscordb_driver_sqlite::SqliteDriver
            .connect(ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            })
            .await
    }
}
fn event(engine: &mut Engine) -> Event {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(event) = engine.try_event() {
            return event;
        }
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(2));
    }
}
fn profile(driver: &str, authentication: Option<SshAuthentication>) -> ConnectionProfile {
    let ssh = authentication.map(|authentication| SshTunnel {
        identity_source: choscordb_driver_api::SshIdentitySource::File,
        options: Default::default(),
        host: "bastion.example".into(),
        port: 22,
        user: "operator".into(),
        identity_file: (authentication == SshAuthentication::PublicKey)
            .then(|| "/tmp/identity".into()),
        authentication,
    });
    let configuration = match driver {
        "mysql" => ProfileConfiguration::Mysql {
            proxy: None,
            host: "localhost".into(),
            port: 3306,
            database: "db".into(),
            user: "u".into(),
            tls: PostgresTls::default(),
            ssh,
        },
        "postgres" => ProfileConfiguration::Postgres {
            proxy: None,
            host: "localhost".into(),
            port: 5432,
            database: "db".into(),
            user: "u".into(),
            tls: PostgresTls::default(),
            ssh,
        },
        _ => ProfileConfiguration::Sqlite {
            path: ":memory:".into(),
            read_only: false,
        },
    };
    ConnectionProfile {
        authentication: Default::default(),
        id: "p".into(),
        name: "Profile".into(),
        group_id: None,
        configuration,
        credential_ref: (driver == "sqlite").then(|| "stale-db".into()),
        proxy_credential_ref: None,
        ssh_jump_credential_refs: Default::default(),
        ssh_private_key_ref: None,
        ssh_jump_private_key_refs: Default::default(),
        tls_credential_ref: None,
        ssh_credential_ref: Some("stale-ssh".into()),
    }
}
#[test]
fn unused_saved_credentials_do_not_block_test_or_connect() {
    for driver in ["sqlite", "postgres", "mysql"] {
        for auth in [None, Some(SshAuthentication::Agent)] {
            let vault = Arc::new(Vault::default());
            let mut engine = Engine::new_with_credentials(
                EngineConfig::default(),
                vec![Arc::new(Driver(driver))],
                vault.clone(),
            )
            .unwrap();
            let profile = profile(driver, auth);
            engine.test_profile(profile.clone(), None, 1).unwrap();
            let result = event(&mut engine);
            assert!(
                matches!(result, Event::ProfileTested { .. }),
                "{driver}: {result:?}"
            );
            engine.connect_profile(profile, None).unwrap();
            let result = event(&mut engine);
            assert!(
                matches!(result, Event::Connected { .. }),
                "{driver}: {result:?}"
            );
            assert!(vault.0.lock().unwrap().is_empty());
        }
    }
}

#[test]
fn required_database_and_ssh_credentials_still_fail_when_missing() {
    for driver in ["postgres", "mysql"] {
        for auth in [
            None,
            Some(SshAuthentication::Password),
            Some(SshAuthentication::PublicKey),
        ] {
            let vault = Arc::new(Vault::default());
            let mut engine = Engine::new_with_credentials(
                EngineConfig::default(),
                vec![Arc::new(Driver(driver))],
                vault.clone(),
            )
            .unwrap();
            let database = auth.is_none();
            let mut profile = profile(driver, auth);
            if database {
                profile.credential_ref = Some("missing-db".into());
            }
            engine.test_profile(profile.clone(), None, 1).unwrap();
            assert!(
                matches!(event(&mut engine), Event::ProfileFailed { error, .. } if error.kind == choscordb_driver_api::ErrorKind::Io)
            );
            engine.connect_profile(profile, None).unwrap();
            assert!(
                matches!(event(&mut engine), Event::ConnectionFailed { error, .. } if error.kind == choscordb_driver_api::ErrorKind::Io)
            );
            let reference = if database { "missing-db" } else { "stale-ssh" };
            assert_eq!(*vault.0.lock().unwrap(), [reference, reference]);
        }
    }
}

struct DelayedDriver;
#[async_trait::async_trait]
impl DatabaseDriver for DelayedDriver {
    fn id(&self) -> &'static str {
        "postgres"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(
        &self,
        _: ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn Connection>> {
        tokio::time::sleep(Duration::from_secs(11)).await;
        choscordb_driver_sqlite::SqliteDriver
            .connect(ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            })
            .await
    }
}

#[test]
fn connection_test_respects_selected_ssh_deadline() {
    let mut engine = Engine::new_with_credentials(
        EngineConfig::default(),
        vec![Arc::new(DelayedDriver)],
        Arc::new(Vault::default()),
    )
    .unwrap();
    let mut profile = profile("postgres", Some(SshAuthentication::Agent));
    if let ProfileConfiguration::Postgres { ssh: Some(ssh), .. } = &mut profile.configuration {
        ssh.options.connect_timeout_seconds = 20;
    } else {
        panic!("SSH PostgreSQL expected")
    }
    engine.test_profile(profile, None, 990).unwrap();
    let end = Instant::now() + Duration::from_secs(15);
    loop {
        if let Some(result) = engine.try_event() {
            assert!(
                matches!(result, Event::ProfileTested { request_token: 990 }),
                "{result:?}"
            );
            break;
        }
        assert!(Instant::now() < end, "connection test did not complete");
        std::thread::sleep(Duration::from_millis(10));
    }
}

struct WaitingVault {
    started: std::sync::mpsc::Sender<()>,
    release: Mutex<std::sync::mpsc::Receiver<()>>,
}
impl CredentialStore for WaitingVault {
    fn get(&self, _: &str) -> choscordb_credentials::Result<Secret> {
        self.started.send(()).unwrap();
        let _ = self.release.lock().unwrap().recv();
        Ok(Secret::new("resolved after deadline"))
    }
    fn put(&self, _: &str, _: &Secret) -> choscordb_credentials::Result<()> {
        unreachable!()
    }
    fn delete(&self, _: &str) -> choscordb_credentials::Result<()> {
        Ok(())
    }
}

#[test]
fn connect_deadline_includes_waiting_for_the_credential_store() {
    let (started, observed) = std::sync::mpsc::channel();
    let (release, wait) = std::sync::mpsc::channel();
    let mut engine = Engine::new_with_credentials(
        EngineConfig::default(),
        vec![Arc::new(Driver("postgres"))],
        Arc::new(WaitingVault {
            started,
            release: Mutex::new(wait),
        }),
    )
    .unwrap();
    let mut profile = profile("postgres", Some(SshAuthentication::Agent));
    profile.credential_ref = Some("pending-db".into());
    if let ProfileConfiguration::Postgres { ssh: Some(ssh), .. } = &mut profile.configuration {
        ssh.options.connect_timeout_seconds = 1;
    } else {
        panic!("SSH PostgreSQL expected")
    }
    engine.connect_profile(profile, None).unwrap();
    observed.recv_timeout(Duration::from_secs(2)).unwrap();
    let end = Instant::now() + Duration::from_secs(9);
    let result = loop {
        if let Some(result) = engine.try_event() {
            break Some(result);
        }
        if Instant::now() >= end {
            break None;
        }
        std::thread::sleep(Duration::from_millis(10));
    };
    // Release the synchronous backend before any assertion/unwind or Engine drop.
    release.send(()).unwrap();
    assert!(
        matches!(result, Some(Event::ConnectionFailed {error,..}) if error.kind == choscordb_driver_api::ErrorKind::Timeout),
        "credential resolution must honor the connection deadline"
    );
}
