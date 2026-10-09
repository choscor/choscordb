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
fn search_reaches_retained_history_beyond_a_page_and_reports_incomplete() {
    let mut engine = Engine::new(Default::default(), vec![]).unwrap();
    for index in 0..25 {
        let mut saved = entry();
        saved.id = format!("entry-{index}");
        saved.timestamp += index;
        saved.sql = if index == 2 || index == 18 {
            format!("SELECT 'Private_%{index}'")
        } else {
            format!("SELECT {index}")
        };
        engine.history_record(saved, index as u64).unwrap();
        assert!(matches!(
            event(&mut engine),
            Event::HistoryRecorded { recorded: true, .. }
        ));
    }
    engine
        .history_search("PRIVATE_%".into(), 1, 0, 100)
        .unwrap();
    assert!(
        matches!(event(&mut engine), Event::HistorySearched { request_token: 100, entries, incomplete: true, .. } if entries.len() == 1 && entries[0].id == "entry-18" && entries[0].sql == "SELECT 'Private_%18'")
    );
    engine
        .history_search("private_%".into(), 10, 0, 101)
        .unwrap();
    assert!(
        matches!(event(&mut engine), Event::HistorySearched { request_token: 101, entries, incomplete: false, .. } if entries.iter().map(|entry| entry.id.as_str()).collect::<Vec<_>>() == vec!["entry-18", "entry-2"])
    );
    assert_eq!(
        engine.history_search("x".repeat(1025), 10, 0, 102),
        Err(SubmitError::ResourceLimit)
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

fn request_token(event: &Event) -> u64 {
    match event {
        Event::WorkspaceSaved { request_token }
        | Event::WorkspaceRestored { request_token, .. }
        | Event::HistoryListed { request_token, .. } => *request_token,
        other => panic!("unexpected recovery event: {other:?}"),
    }
}
#[test]
fn recovery_requests_wait_in_order_behind_the_outstanding_payload() {
    let mut engine = Engine::new(Default::default(), vec![]).unwrap();
    engine.workspace_save(vec![document()], 1).unwrap();
    engine.workspace_save(vec![document()], 2).unwrap();
    engine.history_list(10, 0, 3).unwrap();
    let mut invalid = document();
    invalid.cursor_offset = u64::MAX;
    assert_eq!(
        engine.workspace_save(vec![invalid], 4),
        Err(SubmitError::InvalidInput)
    );
    for token in 5..11 {
        engine.workspace_restore(token).unwrap();
    }
    // One payload is with the worker and eight wait; the next request is refused.
    assert_eq!(engine.workspace_restore(11), Err(SubmitError::QueueFull));
    let answered: Vec<u64> = (0..9).map(|_| request_token(&event(&mut engine))).collect();
    assert_eq!(answered, [1, 2, 3, 5, 6, 7, 8, 9, 10]);
    engine.workspace_restore(12).unwrap();
    assert_eq!(request_token(&event(&mut engine)), 12);
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

#[test]
fn new_documents_receive_distinct_identities() {
    let first = choscordb_core::new_document_id();
    let second = choscordb_core::new_document_id();
    assert_eq!(first.len(), 36, "{first}");
    assert_ne!(first, second);
}

#[test]
fn object_tab_contexts_round_trip_profiles_and_sessions() {
    use choscordb_core::{ObjectTabContext, object_tab_context, parse_object_tab_context};
    assert_eq!(object_tab_context("abc", 7), "profile:abc");
    assert_eq!(object_tab_context("", 7), "session:7");
    assert_eq!(
        parse_object_tab_context("profile:abc"),
        ObjectTabContext::Profile("abc".into())
    );
    assert_eq!(
        parse_object_tab_context("session:7"),
        ObjectTabContext::Session(7)
    );
    assert_eq!(
        parse_object_tab_context("profile:"),
        ObjectTabContext::Unknown
    );
    assert_eq!(
        parse_object_tab_context("session:x"),
        ObjectTabContext::Unknown
    );
    assert_eq!(parse_object_tab_context(""), ObjectTabContext::Unknown);
}
