use choscordb_core::{EditorPreferences, Engine, EngineConfig, Event, SubmitError};
fn event(engine: &mut Engine) -> Event {
    let end = std::time::Instant::now() + std::time::Duration::from_secs(5);
    loop {
        if let Some(event) = engine.try_event() {
            return event;
        }
        assert!(std::time::Instant::now() < end);
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
}
#[test]
fn preferences_are_correlated_gated_and_survive_restart() {
    let dir = tempfile::tempdir().unwrap();
    let config = EngineConfig {
        storage_path: Some(dir.path().join("prefs.sqlite")),
        ..Default::default()
    };
    let mut engine = Engine::new(config.clone(), vec![]).unwrap();
    engine.editor_preferences_get(1).unwrap();
    assert_eq!(engine.workspace_restore(2), Err(SubmitError::QueueFull));
    assert!(
        matches!(event(&mut engine),Event::EditorPreferences{request_token:1,preferences} if preferences==EditorPreferences::default())
    );
    let expected = EditorPreferences {
        font_size: 22,
        ..Default::default()
    };
    engine.editor_preferences_set(expected.clone(), 3).unwrap();
    assert!(
        matches!(event(&mut engine),Event::EditorPreferences{request_token:3,preferences} if preferences==expected)
    );
    let mut invalid = expected.clone();
    invalid.version = 2;
    assert_eq!(
        engine.editor_preferences_set(invalid, 4),
        Err(SubmitError::InvalidInput)
    );
    drop(engine);
    let mut engine = Engine::new(config, vec![]).unwrap();
    engine.editor_preferences_get(5).unwrap();
    assert!(
        matches!(event(&mut engine),Event::EditorPreferences{request_token:5,preferences} if preferences==expected)
    );
}
#[test]
fn initialization_errors_are_correlated_and_do_not_echo_paths() {
    let dir = tempfile::tempdir().unwrap();
    let mut engine = Engine::new(
        EngineConfig {
            storage_path: Some(dir.path().into()),
            ..Default::default()
        },
        vec![],
    )
    .unwrap();
    engine.editor_preferences_get(91).unwrap();
    match event(&mut engine) {
        Event::RecoveryFailed {
            request_token: 91,
            error,
        } => assert!(!error.message.contains(&dir.path().display().to_string())),
        _ => panic!("wrong response"),
    }
    engine
        .editor_preferences_set(EditorPreferences::default(), 92)
        .unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::RecoveryFailed {
            request_token: 92,
            ..
        }
    ));
}
#[test]
fn corrupt_preferences_remain_until_explicit_successful_save() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("metadata.sqlite");
    drop(choscordb_storage::Storage::open(&path).unwrap());
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute(
        "INSERT INTO settings(key,value) VALUES ('editor_preferences',?1)",
        ["private malformed settings"],
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
        engine.editor_preferences_get(token).unwrap();
        match event(&mut engine) {
            Event::RecoveryFailed {
                request_token,
                error,
            } => {
                assert_eq!(request_token, token);
                assert!(!error.message.contains("private"));
            }
            _ => panic!("corrupt settings defaulted"),
        }
    }
    engine
        .editor_preferences_set(EditorPreferences::default(), 3)
        .unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::EditorPreferences {
            request_token: 3,
            ..
        }
    ));
    engine.editor_preferences_get(4).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::EditorPreferences {
            request_token: 4,
            ..
        }
    ));
}
