use choscordb_core::{Engine, EngineConfig, Event, QueryPreferences, SubmitError};
fn event(engine: &mut Engine) -> Event {
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
    loop {
        if let Some(event) = engine.try_event() {
            return event;
        }
        assert!(std::time::Instant::now() < deadline);
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
}
#[test]
fn async_defaults_gate_validation_and_restart() {
    let dir = tempfile::tempdir().unwrap();
    let config = EngineConfig {
        storage_path: Some(dir.path().join("settings.sqlite")),
        ..Default::default()
    };
    let mut engine = Engine::new(config.clone(), vec![]).unwrap();
    engine.query_preferences_get(1).unwrap();
    assert_eq!(engine.workspace_restore(2), Err(SubmitError::QueueFull));
    assert!(
        matches!(event(&mut engine),Event::QueryPreferences{request_token:1,preferences} if preferences==QueryPreferences::default())
    );
    let expected = QueryPreferences {
        timeout_seconds: 30,
        page_size: choscordb_driver_api::PageSize::new(2345).unwrap(),
        ..Default::default()
    };
    engine.query_preferences_set(expected.clone(), 3).unwrap();
    assert!(
        matches!(event(&mut engine),Event::QueryPreferences{request_token:3,preferences} if preferences==expected)
    );
    let mut invalid = expected.clone();
    invalid.version = 2;
    assert_eq!(
        engine.query_preferences_set(invalid, 4),
        Err(SubmitError::InvalidInput)
    );
    drop(engine);
    let mut engine = Engine::new(config, vec![]).unwrap();
    engine.query_preferences_get(5).unwrap();
    assert!(
        matches!(event(&mut engine),Event::QueryPreferences{request_token:5,preferences} if preferences==expected)
    );
}
#[test]
fn corrupt_data_stays_failed_until_explicit_repair_and_errors_are_redacted() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("preferences.sqlite");
    drop(choscordb_storage::Storage::open(&path).unwrap());
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute(
        "INSERT INTO settings(key,value) VALUES ('query_preferences',?1)",
        ["private invalid settings"],
    )
    .unwrap();
    let mut engine = Engine::new(
        EngineConfig {
            storage_path: Some(path),
            ..Default::default()
        },
        vec![],
    )
    .unwrap();
    for token in [1, 2] {
        engine.query_preferences_get(token).unwrap();
        match event(&mut engine) {
            Event::RecoveryFailed {
                request_token,
                error,
            } => {
                assert_eq!(request_token, token);
                assert!(!error.message.contains("private"));
            }
            _ => panic!("corrupt settings were defaulted"),
        }
    }
    engine
        .query_preferences_set(QueryPreferences::default(), 3)
        .unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::QueryPreferences {
            request_token: 3,
            ..
        }
    ));
    engine.query_preferences_get(4).unwrap();
    assert!(
        matches!(event(&mut engine),Event::QueryPreferences{request_token:4,preferences} if preferences==QueryPreferences::default())
    );
}
