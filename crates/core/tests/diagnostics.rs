use choscordb_core::{Engine, EngineConfig, Event};
use choscordb_diagnostics::Service;
use choscordb_driver_api::{ConnectionOptions, PageSize, QueryOptions};
use choscordb_driver_sqlite::SqliteDriver;
use std::{
    sync::Arc,
    thread,
    time::{Duration, Instant},
};

/// Queries that page 100 rows at a time, as these tests fetch.
fn paged() -> QueryOptions {
    QueryOptions {
        page_size: PageSize::new(100).unwrap(),
        ..QueryOptions::default()
    }
}
fn records(folder: &std::path::Path) -> Vec<serde_json::Value> {
    let mut records = Vec::new();
    for entry in std::fs::read_dir(folder).unwrap() {
        let path = entry.unwrap().path();
        if path
            .extension()
            .is_some_and(|extension| extension == "jsonl")
        {
            for line in std::fs::read_to_string(path).unwrap_or_default().lines() {
                if let Ok(record) = serde_json::from_str(line) {
                    records.push(record);
                }
            }
        }
    }
    records
}

#[test]
fn connection_outcome_is_captured_before_ui_drains_engine_events() {
    let root = tempfile::tempdir().unwrap();
    let diagnostics = Arc::new(Service::new(root.path(), "1.2.3", "test-build"));
    assert!(diagnostics.start());
    let mut engine = Engine::new(
        EngineConfig {
            event_capacity: 1,
            ..EngineConfig::default()
        },
        vec![Arc::new(SqliteDriver)],
    )
    .unwrap();
    engine.set_diagnostics(Arc::clone(&diagnostics));
    engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let deadline = Instant::now() + Duration::from_secs(3);
    loop {
        if records(diagnostics.folder_path())
            .iter()
            .filter(|item| item["event"] == "connection_succeeded")
            .count()
            == 2
        {
            break;
        }
        assert!(
            Instant::now() < deadline,
            "connection outcome depended on UI event draining: {:?}",
            records(diagnostics.folder_path())
        );
        thread::sleep(Duration::from_millis(10));
    }
    diagnostics.stop();
}

#[test]
fn query_outcome_is_captured_without_draining_its_engine_events() {
    let root = tempfile::tempdir().unwrap();
    let diagnostics = Arc::new(Service::new(root.path(), "1.2.3", "test-build"));
    assert!(diagnostics.start());
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    engine.set_diagnostics(Arc::clone(&diagnostics));
    let connection = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let deadline = Instant::now() + Duration::from_secs(3);
    while !matches!(engine.try_event(), Some(Event::Connected { .. })) {
        assert!(
            Instant::now() < deadline,
            "SQLite connection did not become ready"
        );
        thread::sleep(Duration::from_millis(10));
    }
    diagnostics.set_open_tabs(4);
    let query = engine
        .execute(connection, "SELECT 1".into(), paged())
        .unwrap();
    engine.fetch_page(query).unwrap();
    let deadline = Instant::now() + Duration::from_secs(3);
    loop {
        if records(diagnostics.folder_path())
            .iter()
            .filter(|item| item["event"] == "query_succeeded")
            .count()
            == 1
        {
            break;
        }
        assert!(
            Instant::now() < deadline,
            "query outcome depended on UI event draining"
        );
        thread::sleep(Duration::from_millis(10));
    }
    assert!(
        records(diagnostics.folder_path())
            .iter()
            .any(|item| item["event"] == "query_succeeded" && item["open_tabs"] == 4),
        "engine outcome lost the latest UI tab count"
    );
    diagnostics.stop();
}
