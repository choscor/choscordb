use choscordb_core::*;
use choscordb_driver_api::*;
use choscordb_driver_sqlite::SqliteDriver;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};

fn wait(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) -> Event {
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        if let Some(event) = engine.try_event()
            && predicate(&event)
        {
            return event;
        }
        assert!(Instant::now() < deadline, "the event did not arrive");
        std::thread::sleep(Duration::from_millis(1));
    }
}

fn deferred_blob(engine: &mut Engine, sql: &str) -> (QueryId, Handle) {
    let connection = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let query = engine
        .execute(
            connection,
            sql.into(),
            QueryOptions {
                page_size: PageSize::new(100).unwrap(),
                ..QueryOptions::default()
            },
        )
        .unwrap();
    engine.fetch_page(query).unwrap();
    let Event::Page { page, .. } = wait(engine, |e| matches!(e, Event::Page { .. })) else {
        unreachable!()
    };
    let Value::Deferred { handle, .. } = page.rows[0][0] else {
        panic!("a large value must defer")
    };
    (query, handle)
}

#[test]
fn a_large_value_streams_into_the_file_without_a_size_limit() {
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let (query, handle) = deferred_blob(
        &mut engine,
        "WITH RECURSIVE n(i) AS (SELECT 0 UNION ALL SELECT i + 1 FROM n WHERE i < 49999) \
         SELECT CAST(group_concat(printf('%05d', i), '') AS BLOB) FROM n",
    );
    let dir = tempfile::tempdir().unwrap();
    let destination = dir.path().join("value.bin");
    engine
        .save_value_to_file(query, handle, destination.clone())
        .unwrap();
    let Event::ValueSaved { bytes, .. } = wait(&mut engine, |e| {
        matches!(e, Event::ValueSaved { .. } | Event::ValueSaveFailed { .. })
    }) else {
        panic!("the value must save")
    };
    let expected: Vec<u8> = (0..50_000)
        .flat_map(|i| format!("{i:05}").into_bytes())
        .collect();
    assert_eq!(bytes, 250_000);
    assert_eq!(std::fs::read(&destination).unwrap(), expected);
}

#[test]
fn a_failed_save_names_the_error_and_leaves_no_file() {
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let (query, handle) = deferred_blob(&mut engine, "SELECT zeroblob(200000)");
    let dir = tempfile::tempdir().unwrap();
    let destination = dir.path().join("missing").join("value.bin");
    engine
        .save_value_to_file(query, handle, destination.clone())
        .unwrap();
    let Event::ValueSaveFailed { error, .. } = wait(&mut engine, |e| {
        matches!(e, Event::ValueSaved { .. } | Event::ValueSaveFailed { .. })
    }) else {
        panic!("the save must fail")
    };
    assert_eq!(error.kind, ErrorKind::Io);
    assert!(!destination.exists());
    let stale = Handle {
        slot: handle.slot + 1,
        ..handle
    };
    engine
        .save_value_to_file(query, stale, dir.path().join("stale.bin"))
        .unwrap();
    let Event::ValueSaveFailed { error, .. } = wait(&mut engine, |e| {
        matches!(e, Event::ValueSaved { .. } | Event::ValueSaveFailed { .. })
    }) else {
        panic!("a stale handle must fail")
    };
    assert_eq!(error.kind, ErrorKind::StaleHandle);
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 0);
}
