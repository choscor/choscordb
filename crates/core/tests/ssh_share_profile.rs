use choscordb_core::{ConnectionProfile, Engine, Event};
use choscordb_driver_api::{ConnectionOptions, DatabaseDriver, DriverCapabilities};
use std::{
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};

struct ObserveSharing(Arc<Mutex<Vec<bool>>>);
#[async_trait::async_trait]
impl DatabaseDriver for ObserveSharing {
    fn id(&self) -> &'static str {
        "postgres"
    }
    fn capabilities(&self) -> DriverCapabilities {
        Default::default()
    }
    async fn connect(
        &self,
        options: ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn choscordb_driver_api::Connection>> {
        let ConnectionOptions::Postgres { ssh: Some(ssh), .. } = options else {
            panic!()
        };
        self.0.lock().unwrap().push(ssh.options.share_tunnels);
        choscordb_driver_sqlite::SqliteDriver
            .connect(ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            })
            .await
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
#[test]
fn test_connection_does_not_lease_a_shared_tunnel() {
    let profile: ConnectionProfile = serde_json::from_value(serde_json::json!({
        "id":"shared","name":"Shared","group_id":null,"credential_ref":null,
        "configuration":{"driver":"postgres","host":"db.example","port":5432,
            "user":"alice","database":"ledger","tls":{"mode":"Disable","root_certificate_path":null},
            "ssh":{"host":"ssh.example","port":22,"user":"ssh-user","authentication":"agent",
                "options":{"share_tunnels":true}}}
    })).unwrap();
    let seen = Arc::new(Mutex::new(Vec::new()));
    let mut engine = Engine::new(
        Default::default(),
        vec![Arc::new(ObserveSharing(seen.clone()))],
    )
    .unwrap();
    engine.test_profile(profile.clone(), None, 1).unwrap();
    assert!(matches!(event(&mut engine), Event::ProfileTested { .. }));
    engine.connect_profile(profile, None).unwrap();
    assert!(matches!(event(&mut engine), Event::Connected { .. }));
    assert_eq!(*seen.lock().unwrap(), [false, true]);
}
