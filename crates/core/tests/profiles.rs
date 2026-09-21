use choscordb_core::{ConnectionProfile, Engine, EngineConfig, Event, ProfileConfiguration};
use std::time::{Duration, Instant};
fn profile(id: &str) -> ConnectionProfile {
    ConnectionProfile {
        id: id.into(),
        name: "Local".into(),
        group_id: None,
        configuration: ProfileConfiguration::Sqlite {
            path: ":memory:".into(),
            read_only: false,
        },
        credential_ref: None,
        ssh_credential_ref: None,
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
#[test]
fn profiles_round_trip_and_survive_engine_reopen() {
    let dir = tempfile::tempdir().unwrap();
    let config = EngineConfig {
        storage_path: Some(dir.path().join("nested/meta.sqlite")),
        ..Default::default()
    };
    let mut engine = Engine::new(config.clone(), vec![]).unwrap();
    engine.profile_save(profile("one"), 1).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::ProfileSaved {
            request_token: 1,
            ..
        }
    ));
    let mut updated = profile("one");
    updated.name = "Updated".into();
    engine.profile_save(updated.clone(), 2).unwrap();
    assert!(
        matches!(event(&mut engine), Event::ProfileSaved { profile, .. } if profile == updated)
    );
    engine
        .profile_duplicate("one".into(), "two".into(), "Copy".into(), 3)
        .unwrap();
    assert!(
        matches!(event(&mut engine), Event::ProfileSaved { profile, .. } if profile.id == "two")
    );
    engine.profile_delete("one".into(), 4).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::ProfileDeleted {
            request_token: 4,
            ..
        }
    ));
    drop(engine);
    let mut engine = Engine::new(config, vec![]).unwrap();
    engine.profile_list(5).unwrap();
    assert!(
        matches!(event(&mut engine), Event::Profiles { request_token: 5, profiles } if profiles.len() == 1 && profiles[0].id == "two")
    );
}
#[test]
fn initialization_failure_is_async_and_redacted() {
    let dir = tempfile::tempdir().unwrap();
    let mut engine = Engine::new(
        EngineConfig {
            storage_path: Some(dir.path().into()),
            ..Default::default()
        },
        vec![],
    )
    .unwrap();
    engine.profile_list(42).unwrap();
    match event(&mut engine) {
        Event::ProfileFailed {
            request_token: 42,
            error,
        } => {
            assert!(!error.message.contains(&dir.path().display().to_string()));
            assert_eq!(error.kind, choscordb_driver_api::ErrorKind::Io);
        }
        other => panic!("unexpected {other:?}"),
    }
}
#[test]
fn bounded_submission_and_shutdown_never_wait_for_event_consumption() {
    let engine = Engine::new(
        EngineConfig {
            command_capacity: 1,
            event_capacity: 1,
            ..Default::default()
        },
        vec![],
    )
    .unwrap();
    let start = Instant::now();
    let mut full = false;
    for token in 0..100 {
        if engine.profile_list(token) == Err(choscordb_core::SubmitError::QueueFull) {
            full = true;
            break;
        }
    }
    assert!(full);
    engine.initiate_shutdown();
    assert_eq!(
        engine.profile_list(100),
        Err(choscordb_core::SubmitError::ShuttingDown)
    );
    drop(engine);
    assert!(start.elapsed() < Duration::from_secs(1));
}
#[test]
fn profile_test_does_not_persist_or_create_a_session() {
    let mut engine = Engine::new(
        EngineConfig::default(),
        vec![std::sync::Arc::new(choscordb_driver_sqlite::SqliteDriver)],
    )
    .unwrap();
    engine.test_profile(profile("test"), None, 7).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::ProfileTested { request_token: 7 }
    ));
    engine.profile_list(8).unwrap();
    assert!(matches!(event(&mut engine), Event::Profiles { profiles, .. } if profiles.is_empty()));
}
#[test]
fn invalid_profiles_are_rejected_before_queueing() {
    let engine = Engine::new(EngineConfig::default(), vec![]).unwrap();
    let mut invalid = profile("bad");
    invalid.name.clear();
    assert_eq!(
        engine.profile_save(invalid, 1),
        Err(choscordb_core::SubmitError::InvalidInput)
    );
    assert_eq!(
        engine.profile_delete("x".repeat(65537), 2),
        Err(choscordb_core::SubmitError::ResourceLimit)
    );
}
