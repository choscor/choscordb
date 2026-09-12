use choscordb_core::{
    EditorDocument, Engine, EngineConfig, Event, HistoryEntry, HistoryPolicy, HistoryStatus,
    SubmitError,
};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
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
fn document() -> EditorDocument {
    EditorDocument {
        id: "doc".into(),
        title: "Unsaved".into(),
        sql: "SELECT 'é';".into(),
        profile_id: Some("absent".into()),
        file_path: Some("/missing.sql".into()),
        cursor_offset: 10,
        selection_anchor: 8,
        modified: true,
    }
}
fn entry() -> HistoryEntry {
    HistoryEntry {
        id: "query".into(),
        profile_id: None,
        sql: "SELECT 'private';".into(),
        timestamp: SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs() as i64,
        duration_ms: 12,
        status: HistoryStatus::Completed,
        row_count: None,
    }
}
#[test]
fn restart_restores_inert_unicode_documents_and_policy() {
    let dir = tempfile::tempdir().unwrap();
    let config = EngineConfig {
        storage_path: Some(dir.path().join("metadata.sqlite")),
        ..Default::default()
    };
    let mut engine = Engine::new(config.clone(), vec![]).unwrap();
    engine.workspace_save(vec![document()], 1).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::WorkspaceSaved { request_token: 1 }
    ));
    engine
        .history_policy_set(
            HistoryPolicy {
                enabled: false,
                ..Default::default()
            },
            2,
        )
        .unwrap();
    assert!(
        matches!(event(&mut engine), Event::HistoryPolicy { request_token: 2, policy } if !policy.enabled)
    );
    drop(engine);
    let mut engine = Engine::new(config, vec![]).unwrap();
    engine.workspace_restore(3).unwrap();
    assert!(
        matches!(event(&mut engine), Event::WorkspaceRestored { request_token: 3, documents } if documents == vec![document()])
    );
    engine.history_record(entry(), 4).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::HistoryRecorded {
            request_token: 4,
            recorded: false
        }
    ));
    engine.history_policy_get(5).unwrap();
    assert!(
        matches!(event(&mut engine), Event::HistoryPolicy { request_token: 5, policy } if !policy.enabled)
    );
}
#[test]
fn record_list_and_clear_are_ordered_and_correlated() {
    let mut engine = Engine::new(Default::default(), vec![]).unwrap();
    let expected = entry();
    engine.history_record(expected.clone(), 10).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::HistoryRecorded {
            request_token: 10,
            recorded: true
        }
    ));
    engine.history_list(10, 0, 11).unwrap();
    assert!(
        matches!(event(&mut engine), Event::HistoryListed { request_token: 11, entries } if entries == vec![expected])
    );
    engine.history_clear(12).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::HistoryCleared { request_token: 12 }
    ));
    engine.history_list(10, 0, 13).unwrap();
    assert!(
        matches!(event(&mut engine), Event::HistoryListed { request_token: 13, entries } if entries.is_empty())
    );
}
#[test]
fn invalid_requests_do_not_enter_queue_and_failures_are_redacted() {
    let mut engine = Engine::new(Default::default(), vec![]).unwrap();
    let mut invalid = document();
    invalid.cursor_offset = 999;
    assert_eq!(
        engine.workspace_save(vec![invalid], 1),
        Err(SubmitError::InvalidInput)
    );
    assert_eq!(
        engine.history_list(1001, 0, 2),
        Err(SubmitError::ResourceLimit)
    );
    assert_eq!(
        engine.history_policy_set(
            HistoryPolicy {
                max_records: 0,
                ..Default::default()
            },
            3
        ),
        Err(SubmitError::InvalidInput)
    );
    assert!(engine.try_event().is_none());
    let dir = tempfile::tempdir().unwrap();
    let mut engine = Engine::new(
        EngineConfig {
            storage_path: Some(dir.path().into()),
            ..Default::default()
        },
        vec![],
    )
    .unwrap();
    engine.history_record(entry(), 44).unwrap();
    match event(&mut engine) {
        Event::RecoveryFailed {
            request_token: 44,
            error,
        } => assert!(!error.message.contains("private")),
        _ => panic!("wrong event"),
    }
    engine.workspace_restore(45).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::RecoveryFailed {
            request_token: 45,
            ..
        }
    ));
}
#[test]
fn bounded_queue_backpressure_and_shutdown_are_nonblocking() {
    let engine = Engine::new(
        EngineConfig {
            command_capacity: 1,
            event_capacity: 1,
            ..Default::default()
        },
        vec![],
    )
    .unwrap();
    // With events deliberately undrained the single worker can hold at most one
    // response, one pending response and one queued request.
    let start = Instant::now();
    let mut full = false;
    for token in 0..20 {
        match engine.workspace_restore(token) {
            Ok(()) => {}
            Err(SubmitError::QueueFull) => {
                full = true;
                break;
            }
            other => panic!("unexpected submit result: {other:?}"),
        }
    }
    assert!(full);
    assert!(start.elapsed() < Duration::from_secs(1));
    engine.initiate_shutdown();
    assert_eq!(engine.workspace_restore(99), Err(SubmitError::ShuttingDown));
    assert_eq!(engine.history_clear(100), Err(SubmitError::ShuttingDown));
    drop(engine);
    assert!(start.elapsed() < Duration::from_secs(1));
}

#[test]
fn one_recovery_payload_remains_outstanding_until_response_consumed() {
    let mut engine = Engine::new(Default::default(), vec![]).unwrap();
    engine.workspace_save(vec![document()], 1).unwrap();
    assert_eq!(
        engine.workspace_save(vec![document()], 2),
        Err(SubmitError::QueueFull)
    );
    assert_eq!(engine.history_list(10, 0, 3), Err(SubmitError::QueueFull));
    assert!(matches!(
        event(&mut engine),
        Event::WorkspaceSaved { request_token: 1 }
    ));
    let mut invalid = document();
    invalid.cursor_offset = u64::MAX;
    assert_eq!(
        engine.workspace_save(vec![invalid], 7),
        Err(SubmitError::InvalidInput)
    );
    engine.workspace_restore(4).unwrap();
    let mut invalid = document();
    invalid.cursor_offset = u64::MAX;
    assert_eq!(
        engine.workspace_save(vec![invalid], 8),
        Err(SubmitError::QueueFull)
    );
    std::thread::sleep(Duration::from_millis(30));
    assert_eq!(engine.workspace_restore(5), Err(SubmitError::QueueFull));
    assert!(
        matches!(event(&mut engine), Event::WorkspaceRestored { documents, .. } if documents == vec![document()])
    );
    engine.workspace_restore(6).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::WorkspaceRestored {
            request_token: 6,
            ..
        }
    ));
}
#[test]
fn retry_reopens_storage_after_transient_initial_failure() {
    let dir = tempfile::tempdir().unwrap();
    let parent = dir.path().join("blocked");
    std::fs::write(&parent, "blocking file").unwrap();
    let mut engine = Engine::new(
        EngineConfig {
            storage_path: Some(parent.join("meta.sqlite")),
            ..Default::default()
        },
        vec![],
    )
    .unwrap();
    engine.workspace_restore(1).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::RecoveryFailed {
            request_token: 1,
            ..
        }
    ));
    std::fs::remove_file(&parent).unwrap();
    engine.workspace_restore(2).unwrap();
    assert!(
        matches!(event(&mut engine), Event::WorkspaceRestored { request_token: 2, documents } if documents.is_empty())
    );
    engine.workspace_save(vec![document()], 3).unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::WorkspaceSaved { request_token: 3 }
    ));
}
