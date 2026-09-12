use choscordb_core::*;
use choscordb_driver_api::*;
use choscordb_driver_sqlite::SqliteDriver;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};
fn result(engine: &mut Engine) -> Event {
    let deadline = Instant::now() + Duration::from_secs(4);
    loop {
        if let Some(event) = engine.try_event()
            && matches!(event, Event::StoredPage { .. } | Event::QueryFailed { .. })
        {
            return event;
        }
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(1));
    }
}
#[test]
fn hot_original_page_survives_disk_corruption() {
    let dir = tempfile::tempdir().unwrap();
    let mut engine = Engine::new(
        EngineConfig {
            result_store_directory: Some(dir.path().into()),
            ..Default::default()
        },
        vec![Arc::new(SqliteDriver)],
    )
    .unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let q = engine
        .execute(c, "SELECT 73".into(), QueryOptions::default())
        .unwrap();
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    assert!(matches!(result(&mut engine), Event::StoredPage { .. }));
    assert!(engine.cache_usage().resident_bytes > 0);
    let store = std::fs::read_dir(dir.path())
        .unwrap()
        .next()
        .unwrap()
        .unwrap()
        .path();
    for file in std::fs::read_dir(store).unwrap() {
        std::fs::write(file.unwrap().path(), b"broken").unwrap();
    }
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    let event = result(&mut engine);
    assert!(
        matches!(event, Event::StoredPage {first_row: 0, ref page, ..} if page.rows == vec![vec![Value::Integer(73)]]),
        "{event:?}"
    );
    assert_eq!(engine.cache_usage().hits, 1);
    assert_eq!(engine.cache_usage().misses, 0);
    drop(event);
    engine.release_query(q).unwrap();
    let deadline = Instant::now() + Duration::from_secs(4);
    while engine.cache_usage().resident_bytes != 0 {
        assert!(Instant::now() < deadline);
        drop(engine.try_event());
        std::thread::sleep(Duration::from_millis(1));
    }
}

#[test]
fn pressure_evicts_old_pages_but_preserves_visible_copy_and_disk_fallback() {
    let dir = tempfile::tempdir().unwrap();
    let mut engine = Engine::new(
        EngineConfig {
            result_store_directory: Some(dir.path().into()),
            page_memory: PageMemoryConfig {
                per_result_bytes: 1024 * 1024,
                global_bytes: 1024 * 1024,
            },
            ..Default::default()
        },
        vec![Arc::new(SqliteDriver)],
    )
    .unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let q = engine.execute(c, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<5000) SELECT x, printf('%0200d', x) FROM c".into(), QueryOptions::default()).unwrap();
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    let Event::StoredPage {
        page: visible,
        mut lease,
        ..
    } = result(&mut engine)
    else {
        panic!("page expected");
    };
    lease.shrink_to(visible.estimated_bytes()).unwrap();
    for index in 1..30 {
        engine
            .fetch_page_at(q, index, PageSize::new(100).unwrap())
            .unwrap();
        assert!(matches!(result(&mut engine), Event::StoredPage {page, ..} if page.index == index));
    }
    assert_eq!(visible.rows[0][0], Value::Integer(1));
    assert!(engine.cache_usage().resident_bytes > 0);
    assert!(engine.memory_usage().peak_bytes <= 1024 * 1024);
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    assert!(
        matches!(result(&mut engine), Event::StoredPage {first_row: 0, page, ..} if page.rows == visible.rows)
    );
    assert_eq!(engine.cache_usage().misses, 1);
    // Fetch later pages again to evict the reread, then prove a miss really uses disk.
    for index in 1..30 {
        engine
            .fetch_page_at(q, index, PageSize::new(100).unwrap())
            .unwrap();
        assert!(matches!(result(&mut engine), Event::StoredPage { .. }));
    }
    let store = std::fs::read_dir(dir.path())
        .unwrap()
        .next()
        .unwrap()
        .unwrap()
        .path();
    for file in std::fs::read_dir(store).unwrap() {
        std::fs::write(file.unwrap().path(), b"broken").unwrap();
    }
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    assert!(
        matches!(result(&mut engine), Event::QueryFailed {error, ..} if error.kind == ErrorKind::Io)
    );
    assert_eq!(visible.rows[0][0], Value::Integer(1));
    engine.disconnect(c).unwrap();
    let deadline = Instant::now() + Duration::from_secs(4);
    loop {
        if matches!(engine.try_event(), Some(Event::Disconnected { .. })) {
            break;
        }
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(1));
    }
    assert_eq!(engine.cache_usage().resident_bytes, 0);
    assert_eq!(engine.memory_usage().used_bytes, lease.reserved_bytes());
    drop(lease);
    assert_eq!(engine.memory_usage().used_bytes, 0);
}

#[test]
fn global_cache_reclaims_across_connections_and_cancelled_pages_remain_hot() {
    let mut engine = Engine::new(
        EngineConfig {
            page_memory: PageMemoryConfig {
                per_result_bytes: 1024 * 1024,
                global_bytes: 1024 * 1024,
            },
            ..Default::default()
        },
        vec![Arc::new(SqliteDriver)],
    )
    .unwrap();
    let first = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let q = engine.execute(first, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<5000) SELECT x, printf('%0200d', x) FROM c".into(), QueryOptions::default()).unwrap();
    for index in 0..25 {
        engine
            .fetch_page_at(q, index, PageSize::new(100).unwrap())
            .unwrap();
        assert!(matches!(result(&mut engine), Event::StoredPage { .. }));
    }
    engine.cancel(q).unwrap();
    assert!(
        matches!(result(&mut engine), Event::QueryFailed {error, ..} if error.kind == ErrorKind::Cancelled)
    );
    engine
        .fetch_page_at(q, 24, PageSize::new(100).unwrap())
        .unwrap();
    assert!(matches!(result(&mut engine), Event::StoredPage {page, ..} if !page.has_more));
    assert_eq!(engine.cache_usage().hits, 1);
    let second = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let other = engine.execute(second, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<5000) SELECT x, printf('%0200d', x) FROM c".into(), QueryOptions::default()).unwrap();
    for index in 0..25 {
        engine
            .fetch_page_at(other, index, PageSize::new(100).unwrap())
            .unwrap();
        assert!(matches!(result(&mut engine), Event::StoredPage { .. }));
    }
    engine.cancel(other).unwrap();
    assert!(
        matches!(result(&mut engine), Event::QueryFailed {error, ..} if error.kind == ErrorKind::Cancelled)
    );
    engine
        .fetch_page_at(q, 24, PageSize::new(100).unwrap())
        .unwrap();
    assert!(matches!(result(&mut engine), Event::StoredPage {page, ..} if !page.has_more));
    assert_eq!(engine.cache_usage().misses, 1);
    assert!(engine.memory_usage().peak_bytes <= 1024 * 1024);
    engine.release_query(q).unwrap();
    engine.release_query(other).unwrap();
    let deadline = Instant::now() + Duration::from_secs(4);
    while engine.memory_usage().used_bytes != 0 {
        drop(engine.try_event());
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(1));
    }
    assert_eq!(engine.cache_usage().resident_bytes, 0);
}
