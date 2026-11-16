use choscordb_core::*;
use choscordb_driver_api::*;
use choscordb_driver_sqlite::SqliteDriver;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};

fn profile(safety: SafetyLevel) -> ConnectionProfile {
    ConnectionProfile {
        id: "local".into(),
        name: "Local".into(),
        group_id: None,
        configuration: ProfileConfiguration::Sqlite {
            path: ":memory:".into(),
            read_only: false,
        },
        credential_ref: None,
        proxy_credential_ref: None,
        ssh_jump_credential_refs: Default::default(),
        ssh_jump_private_key_refs: Default::default(),
        environment: Default::default(),
        environment_label: None,
        color: Default::default(),
        safety,
        tls_credential_ref: None,
        ssh_credential_ref: None,
        ssh_private_key_ref: None,
        row_limit: 0,
    }
}

fn outcome(engine: &mut Engine) -> std::result::Result<(), String> {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        match engine.try_event() {
            Some(Event::QueryFailed { error, .. }) => return Err(error.message),
            Some(Event::ConnectionFailed { error, .. }) => return Err(error.message),
            Some(Event::QueryState {
                state: QueryState::Completed { .. },
                ..
            }) => return Ok(()),
            _ => {}
        }
        assert!(Instant::now() < deadline, "no query outcome");
        std::thread::sleep(Duration::from_millis(2));
    }
}

fn create_table(safety: SafetyLevel) -> std::result::Result<(), String> {
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let connection = engine.connect_profile(profile(safety), None).unwrap();
    let query = engine
        .execute(
            connection,
            "CREATE TABLE t(a INTEGER)".into(),
            QueryOptions::default(),
        )
        .unwrap();
    engine.fetch_page_at(query, 0).unwrap();
    outcome(&mut engine)
}

#[test]
fn read_only_profiles_lock_the_database_session() {
    let error = create_table(SafetyLevel::ReadOnly).expect_err("write ran on a read-only session");
    assert!(error.contains("readonly"), "{error}");
    assert_eq!(create_table(SafetyLevel::ConfirmWrites), Ok(()));
}
