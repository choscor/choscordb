use choscordb_core::*;
#[test]
fn memory_budgets_reject_values_below_one_mib() {
    let mut config = EngineConfig::default();
    config.page_memory.global_bytes = 1024 * 1024 - 1;
    assert!(Engine::new(config, vec![]).is_err());
}

use choscordb_driver_api::*;
use choscordb_driver_sqlite::SqliteDriver;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};
fn next(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) -> Event {
    let until = Instant::now() + Duration::from_secs(4);
    loop {
        if let Some(event) = engine.try_event() {
            assert!(
                !matches!(&event, Event::QueryFailed { error, .. } if !matches!(error.kind, ErrorKind::Cancelled | ErrorKind::Disconnected)),
                "{event:?}"
            );
            if predicate(&event) {
                return event;
            }
        }
        assert!(Instant::now() < until, "event timed out");
        std::thread::sleep(Duration::from_millis(1));
    }
}
fn engine() -> Engine {
    Engine::new(
        EngineConfig {
            page_memory: PageMemoryConfig {
                per_result_bytes: 1024 * 1024,
                global_bytes: 1024 * 1024,
            },
            ..Default::default()
        },
        vec![Arc::new(SqliteDriver)],
    )
    .unwrap()
}
fn connection(engine: &mut Engine) -> ConnectionId {
    engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap()
}
#[test]
fn consumer_lease_retains_budget_and_shrinking_wakes_fetch() {
    let mut engine = engine();
    let c = connection(&mut engine);
    let q = engine.execute(c, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<300) SELECT x FROM c".into(), QueryOptions::default()).unwrap();
    engine.fetch_page(q, PageSize::new(100).unwrap()).unwrap();
    let Event::Page { mut lease, .. } = next(&mut engine, |e| matches!(e, Event::Page { .. }))
    else {
        unreachable!()
    };
    assert!(lease.reserved_bytes() > 500_000);
    engine.fetch_page(q, PageSize::new(100).unwrap()).unwrap();
    std::thread::sleep(Duration::from_millis(30));
    while let Some(event) = engine.try_event() {
        assert!(!matches!(event, Event::Page { .. }));
    }
    assert!(lease.shrink_to(lease.reserved_bytes()).is_err());
    lease.shrink_to(1000).unwrap();
    let page = next(&mut engine, |e| matches!(e, Event::Page { .. }));
    assert!(engine.memory_usage().peak_bytes <= 1024 * 1024);
    engine.release_query(q).unwrap();
    engine.disconnect(c).unwrap();
    next(&mut engine, |e| matches!(e, Event::Disconnected { .. }));
    assert!(engine.memory_usage().used_bytes > 500_000);
    drop(page);
    assert_eq!(engine.memory_usage().used_bytes, 1256);
    drop(lease);
    assert_eq!(engine.memory_usage().used_bytes, 0);
}

#[test]
fn cancelling_source_budget_wait_never_starts_sqlite_write() {
    let mut engine = engine();
    let first = connection(&mut engine);
    let q = engine
        .execute(first, "SELECT 1".into(), QueryOptions::default())
        .unwrap();
    drop(next(
        &mut engine,
        |e| matches!(e, Event::Schema {query, ..} if *query == q),
    ));
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("queued.sqlite");
    let observer = rusqlite::Connection::open(&path).unwrap();
    observer.execute_batch("CREATE TABLE t(x)").unwrap();
    let second = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path,
                read_only: false,
            },
        )
        .unwrap();
    let write = engine
        .execute(
            second,
            "INSERT INTO t VALUES (42)".into(),
            QueryOptions::default(),
        )
        .unwrap();
    std::thread::sleep(Duration::from_millis(30));
    engine.cancel(write).unwrap();
    let until = Instant::now() + Duration::from_secs(3);
    loop {
        if let Some(event) = engine.try_event() {
            if let Event::QueryFailed { query, error } = event {
                assert_eq!(query, write);
                assert_eq!(error.kind, ErrorKind::Cancelled);
                break;
            }
            assert!(!matches!(event, Event::Schema {query, ..} if query == write));
        }
        assert!(Instant::now() < until);
        std::thread::sleep(Duration::from_millis(1));
    }
    engine.release_query(q).unwrap();
    let count: i64 = observer
        .query_row("SELECT count(*) FROM t", [], |r| r.get(0))
        .unwrap();
    assert_eq!(count, 0);
}

#[test]
fn old_generation_consumer_reservation_survives_reuse_and_wakes_other_connection() {
    let mut engine = engine();
    let first = connection(&mut engine);
    let old = engine
        .execute(first, "SELECT 1".into(), QueryOptions::default())
        .unwrap();
    engine.fetch_page(old, PageSize::new(100).unwrap()).unwrap();
    let held = next(&mut engine, |e| matches!(e, Event::Page { .. }));
    engine.release_query(old).unwrap();
    engine.disconnect(first).unwrap();
    next(&mut engine, |e| matches!(e, Event::Disconnected { .. }));
    let second = connection(&mut engine);
    let new = engine
        .execute(second, "SELECT 2".into(), QueryOptions::default())
        .unwrap();
    assert_ne!(old, new);
    // The new source fits, but its schema transfer must wait on the old consumer.
    std::thread::sleep(Duration::from_millis(30));
    while let Some(event) = engine.try_event() {
        assert!(!matches!(event, Event::Schema {query, ..} if query == new));
    }
    drop(held);
    drop(next(
        &mut engine,
        |e| matches!(e, Event::Schema {query, ..} if *query == new),
    ));
    engine.fetch_page(new, PageSize::new(100).unwrap()).unwrap();
    let Event::Page { page, .. } = next(&mut engine, |e| matches!(e, Event::Page { .. })) else {
        unreachable!()
    };
    assert_eq!(page.rows[0][0], Value::Integer(2));
    assert!(engine.memory_usage().peak_bytes <= 1024 * 1024);
}

#[test]
fn disconnect_unblocks_completed_page_wait_without_dropping_consumer() {
    let mut engine = engine();
    let c = connection(&mut engine);
    let q = engine
        .execute(c, "SELECT 1".into(), QueryOptions::default())
        .unwrap();
    engine.fetch_page(q, PageSize::new(100).unwrap()).unwrap();
    let held = next(&mut engine, |e| matches!(e, Event::Page { .. }));
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    std::thread::sleep(Duration::from_millis(20));
    engine.disconnect(c).unwrap();
    let until = Instant::now() + Duration::from_secs(3);
    loop {
        if matches!(engine.try_event(), Some(Event::Disconnected { .. })) {
            break;
        }
        assert!(Instant::now() < until);
        std::thread::sleep(Duration::from_millis(1));
    }
    assert!(engine.memory_usage().used_bytes > 500_000);
    drop(held);
    assert_eq!(engine.memory_usage().used_bytes, 0);
}

#[test]
fn release_unblocks_source_wait_without_releasing_other_connection() {
    let mut engine = engine();
    let a = connection(&mut engine);
    let first = engine
        .execute(a, "SELECT 1".into(), QueryOptions::default())
        .unwrap();
    drop(next(
        &mut engine,
        |e| matches!(e, Event::Schema {query, ..} if *query == first),
    ));
    let b = connection(&mut engine);
    let waiting = engine
        .execute(b, "SELECT 2".into(), QueryOptions::default())
        .unwrap();
    std::thread::sleep(Duration::from_millis(20));
    engine.release_query(waiting).unwrap();
    let until = Instant::now() + Duration::from_secs(3);
    loop {
        if matches!(engine.try_event(), Some(Event::QueryFailed {query, error}) if query == waiting && error.kind == ErrorKind::Cancelled)
        {
            break;
        }
        assert!(Instant::now() < until);
        std::thread::sleep(Duration::from_millis(1));
    }
}

#[test]
fn completed_tiny_results_release_source_capacity_for_fifth_connection() {
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let mut held = Vec::new();
    for _ in 0..6 {
        let c = connection(&mut engine);
        let q = engine
            .execute(c, "SELECT 1".into(), QueryOptions::default())
            .unwrap();
        engine.fetch_page(q, PageSize::new(100).unwrap()).unwrap();
        let Event::Page { mut lease, .. } = next(
            &mut engine,
            |e| matches!(e, Event::Page {query, ..} if *query == q),
        ) else {
            unreachable!()
        };
        lease.shrink_to(1024).unwrap();
        held.push(lease);
        next(
            &mut engine,
            |e| matches!(e, Event::QueryFinished {query, ..} if *query == q),
        );
    }
    assert!(engine.memory_usage().source_bytes < 1024 * 1024);
    assert!(engine.memory_usage().peak_bytes <= 256 * 1024 * 1024);
}

#[test]
fn cancelled_partial_result_can_revisit_stored_pages() {
    let mut engine = engine();
    let c = connection(&mut engine);
    let q = engine.execute(c, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<300) SELECT x FROM c".into(), QueryOptions::default()).unwrap();
    for _ in 0..2 {
        engine.fetch_page(q, PageSize::new(100).unwrap()).unwrap();
        drop(next(&mut engine, |e| matches!(e, Event::Page { .. })));
    }
    engine.cancel(q).unwrap();
    next(
        &mut engine,
        |e| matches!(e, Event::QueryFailed {query, error} if *query == q && error.kind == ErrorKind::Cancelled),
    );
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    let Event::StoredPage { page, .. } =
        next(&mut engine, |e| matches!(e, Event::StoredPage { .. }))
    else {
        unreachable!()
    };
    assert_eq!(page.rows[0][0], Value::Integer(1));
    assert_eq!(page.rows.len(), 100);
}
