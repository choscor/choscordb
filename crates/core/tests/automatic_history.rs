use choscordb_core::{Engine, EngineConfig, Event, HistoryStatus};
use choscordb_driver_api::{ConnectionOptions, PageSize, QueryOptions};
use std::{
    sync::Arc,
    time::{Duration, Instant},
};
fn until(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) -> Event {
    let end = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(event) = engine.try_event()
            && predicate(&event)
        {
            return event;
        }
        assert!(Instant::now() < end);
        std::thread::sleep(Duration::from_millis(1));
    }
}
#[test]
fn execution_records_original_rows_profile_and_failure() {
    let mut engine = Engine::new(
        EngineConfig::default(),
        vec![Arc::new(choscordb_driver_sqlite::SqliteDriver)],
    )
    .unwrap();
    let connection = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::Connected { .. }));
    let query = engine
        .execute_with_profile(
            connection,
            "SELECT 1 UNION ALL SELECT 2".into(),
            QueryOptions::default(),
            Some("profile".into()),
        )
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::Schema { .. }));
    engine
        .fetch_page(query, PageSize::new(100).unwrap())
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::QueryFinished { .. }));
    engine
        .fetch_page_at(query, 0, PageSize::new(100).unwrap())
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::StoredPage { .. }));
    engine.history_list(10, 0, 1).unwrap();
    let Event::HistoryListed { entries, .. } =
        until(&mut engine, |e| matches!(e, Event::HistoryListed { .. }))
    else {
        unreachable!()
    };
    assert_eq!(entries.len(), 1);
    assert_eq!(entries[0].profile_id.as_deref(), Some("profile"));
    assert_eq!(entries[0].row_count, Some(2));
    assert_eq!(entries[0].status, HistoryStatus::Completed);
    engine
        .execute(
            connection,
            "SELECT missing_column".into(),
            QueryOptions::default(),
        )
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::QueryFailed { .. }));
    engine.history_list(10, 0, 2).unwrap();
    let Event::HistoryListed { entries, .. } =
        until(&mut engine, |e| matches!(e, Event::HistoryListed { .. }))
    else {
        unreachable!()
    };
    assert_eq!(entries.len(), 2);
    assert!(
        entries
            .iter()
            .any(|e| e.status == HistoryStatus::Failed && e.sql == "SELECT missing_column")
    );
}
#[test]
fn cancellation_records_without_consuming_ui_events_and_disconnect_flushes() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("history.sqlite");
    let mut engine = Engine::new(
        EngineConfig {
            storage_path: Some(path.clone()),
            ..Default::default()
        },
        vec![Arc::new(choscordb_driver_sqlite::SqliteDriver)],
    )
    .unwrap();
    let connection = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::Connected { .. }));
    let query = engine
        .execute(connection, "SELECT 42".into(), QueryOptions::default())
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::Schema { .. }));
    engine.cancel(query).unwrap();
    let end = Instant::now() + Duration::from_secs(5);
    loop {
        let storage = choscordb_storage::Storage::open(&path).unwrap();
        let entries = storage.history(10, 0).unwrap();
        if !entries.is_empty() {
            assert_eq!(entries[0].status, HistoryStatus::Cancelled);
            assert_eq!(entries[0].row_count, None);
            break;
        }
        assert!(Instant::now() < end);
        std::thread::sleep(Duration::from_millis(2));
    }
    let query = engine
        .execute(connection, "SELECT 99".into(), QueryOptions::default())
        .unwrap();
    until(
        &mut engine,
        |e| matches!(e,Event::Schema{query:q,..} if *q==query),
    );
    engine.disconnect(connection).unwrap();
    until(&mut engine, |e| matches!(e, Event::Disconnected { .. }));
    engine.history_flush(7).unwrap();
    until(&mut engine, |e| {
        matches!(e, Event::HistoryFlushed { request_token: 7 })
    });
    drop(engine);
    let entries = choscordb_storage::Storage::open(path)
        .unwrap()
        .history(10, 0)
        .unwrap();
    assert_eq!(entries.len(), 2);
    assert!(
        entries
            .iter()
            .any(|e| e.sql == "SELECT 99" && e.status == HistoryStatus::Disconnected)
    );
}
#[test]
fn partial_release_records_unknown_count_and_disabled_policy_prevents_new_records() {
    let mut engine = Engine::new(
        Default::default(),
        vec![Arc::new(choscordb_driver_sqlite::SqliteDriver)],
    )
    .unwrap();
    let connection = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::Connected { .. }));
    let query = engine
        .execute(connection, "SELECT 1".into(), QueryOptions::default())
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::Schema { .. }));
    engine.release_query(query).unwrap();
    until(&mut engine, |e| matches!(e, Event::QueryFailed { .. }));
    engine.history_list(10, 0, 1).unwrap();
    let Event::HistoryListed { entries, .. } =
        until(&mut engine, |e| matches!(e, Event::HistoryListed { .. }))
    else {
        unreachable!()
    };
    assert_eq!(entries.len(), 1);
    assert_eq!(entries[0].row_count, None);
    engine.history_clear(2).unwrap();
    until(&mut engine, |e| matches!(e, Event::HistoryCleared { .. }));
    engine
        .history_policy_set(
            choscordb_core::HistoryPolicy {
                enabled: false,
                ..Default::default()
            },
            3,
        )
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::HistoryPolicy { .. }));
    engine
        .execute(connection, "SELECT invalid".into(), QueryOptions::default())
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::QueryFailed { .. }));
    engine.history_list(10, 0, 4).unwrap();
    assert!(
        matches!(until(&mut engine,|e|matches!(e,Event::HistoryListed{..})),Event::HistoryListed{entries,..} if entries.is_empty())
    );
}
#[test]
fn history_storage_failure_is_redacted_and_does_not_fail_successful_query() {
    let dir = tempfile::tempdir().unwrap();
    let mut engine = Engine::new(
        EngineConfig {
            storage_path: Some(dir.path().into()),
            ..Default::default()
        },
        vec![Arc::new(choscordb_driver_sqlite::SqliteDriver)],
    )
    .unwrap();
    let connection = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::Connected { .. }));
    let query = engine
        .execute(
            connection,
            "SELECT 'private-history-text'".into(),
            QueryOptions::default(),
        )
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::Schema { .. }));
    engine
        .fetch_page(query, PageSize::new(100).unwrap())
        .unwrap();
    let mut finished = false;
    let mut failed_history = false;
    let end = Instant::now() + Duration::from_secs(5);
    while !finished || !failed_history {
        if let Some(event) = engine.try_event() {
            match event {
                Event::QueryFinished { .. } => finished = true,
                Event::HistoryWriteFailed {
                    query: recorded,
                    error,
                } => {
                    assert_eq!(recorded, query);
                    assert!(!error.message.contains("private-history-text"));
                    failed_history = true;
                }
                Event::QueryFailed { .. } => panic!("history must not fail execution"),
                _ => {}
            }
        }
        assert!(Instant::now() < end);
        std::thread::sleep(Duration::from_millis(1));
    }
}
