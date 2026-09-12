use choscordb_core::{
    Accent, AppearanceLayout, Density, Engine, EngineConfig, Event, SubmitError, ThemeMode,
};

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
fn appearance_get_set_reset_are_correlated_gated_and_persistent() {
    let directory = tempfile::tempdir().unwrap();
    let config = EngineConfig {
        storage_path: Some(directory.path().join("appearance.sqlite")),
        ..Default::default()
    };
    let mut engine = Engine::new(config.clone(), vec![]).unwrap();
    engine.appearance_layout_get(1).unwrap();
    assert_eq!(engine.appearance_layout_get(2), Err(SubmitError::QueueFull));
    assert!(matches!(
        event(&mut engine),
        Event::AppearanceLayout {
            request_token: 1,
            appearance: None
        }
    ));

    let expected = AppearanceLayout {
        theme: ThemeMode::Dark,
        density: Density::Comfortable,
        accent: Accent::Custom("#1267A8".into()),
        ..Default::default()
    };
    engine.appearance_layout_set(expected.clone(), 3).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::AppearanceLayout { request_token: 3, appearance: Some(value) } if value == expected
    ));
    drop(engine);

    let mut engine = Engine::new(config, vec![]).unwrap();
    engine.appearance_layout_get(4).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::AppearanceLayout { request_token: 4, appearance: Some(value) } if value == expected
    ));
    engine.appearance_layout_reset(5).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::AppearanceLayout {
            request_token: 5,
            appearance: None
        }
    ));
    engine.appearance_layout_get(6).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::AppearanceLayout {
            request_token: 6,
            appearance: None
        }
    ));
}

#[test]
fn invalid_submission_releases_the_shared_queue() {
    let mut engine = Engine::new(EngineConfig::default(), vec![]).unwrap();
    let mut invalid = AppearanceLayout::default();
    invalid.geometry.width = 1;
    assert_eq!(
        engine.appearance_layout_set(invalid, 1),
        Err(SubmitError::InvalidInput)
    );
    engine.appearance_layout_get(2).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::AppearanceLayout {
            request_token: 2,
            appearance: None
        }
    ));
}

#[test]
fn corrupt_and_unsupported_failures_are_distinct_and_terminal() {
    for (stored, kind, message) in [
        (
            "{bad json",
            choscordb_driver_api::ErrorKind::InvalidInput,
            "corrupt",
        ),
        (
            r##"{"version":2,"theme":"system","density":"compact","accent":{"kind":"preset","value":"cobalt"},"layout":{"navigator_width":280,"editor_results_split":600,"history_height":220,"navigator_visible":true,"history_visible":false},"geometry":{"x":0,"y":0,"width":1280,"height":900,"maximized":false}}"##,
            choscordb_driver_api::ErrorKind::Unsupported,
            "version 2",
        ),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("appearance.sqlite");
        drop(choscordb_storage::Storage::open(&path).unwrap());
        let db = rusqlite::Connection::open(&path).unwrap();
        db.execute(
            "INSERT INTO appearance_layout(singleton,value) VALUES (1,?1)",
            [stored],
        )
        .unwrap();
        drop(db);
        let mut engine = Engine::new(
            EngineConfig {
                storage_path: Some(path),
                ..Default::default()
            },
            vec![],
        )
        .unwrap();
        engine.appearance_layout_get(10).unwrap();
        match event(&mut engine) {
            Event::RecoveryFailed {
                request_token: 10,
                error,
            } => {
                assert_eq!(error.kind, kind);
                assert!(error.message.contains(message));
                assert!(!error.message.contains(stored));
            }
            _ => panic!("wrong response"),
        }
        engine.appearance_layout_reset(11).unwrap();
        assert!(matches!(
            event(&mut engine),
            Event::AppearanceLayout {
                request_token: 11,
                appearance: None
            }
        ));
    }
}
