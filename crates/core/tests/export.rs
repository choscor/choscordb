use choscordb_core::*;
use choscordb_driver_api::*;
use choscordb_driver_sqlite::SqliteDriver;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};
fn wait(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) -> Event {
    let until = Instant::now() + Duration::from_secs(10);
    loop {
        if let Some(event) = engine.try_event()
            && predicate(&event)
        {
            return event;
        }
        assert!(Instant::now() < until, "event timed out");
        std::thread::sleep(Duration::from_millis(1));
    }
}
#[test]
fn export_starts_at_zero_after_browsing_and_advances_original_cursor() {
    let dir = tempfile::tempdir().unwrap();
    let mut engine = Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let q=engine.execute(c,"WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<251) SELECT x FROM c".into(),Default::default()).unwrap();
    engine.fetch_page(q, PageSize::new(100).unwrap()).unwrap();
    drop(wait(&mut engine, |e| matches!(e, Event::Page { .. })));
    let path = dir.path().join("rows.csv");
    let id = engine
        .start_export(q, path.clone(), ExportFormat::Csv)
        .unwrap();
    let event = wait(&mut engine, |e| {
        matches!(e, Event::ExportFinished { .. } | Event::ExportFailed { .. })
    });
    assert!(
        matches!(event,Event::ExportFinished{export,rows:251,..} if export==id),
        "{event:?}"
    );
    let expected = std::iter::once("\"x\"\r\n".to_string())
        .chain((1..=251).map(|i| format!("\"{i}\"\r\n")))
        .collect::<String>();
    assert_eq!(std::fs::read_to_string(path).unwrap(), expected);
    assert_eq!(engine.cancel_export(id), Err(SubmitError::StaleHandle));
}

#[test]
fn update_returning_export_never_executes_the_write_twice() {
    let dir = tempfile::tempdir().unwrap();
    let db = dir.path().join("db.sqlite");
    let observer = rusqlite::Connection::open(&db).unwrap();
    observer
        .execute_batch("CREATE TABLE t(x INTEGER); INSERT INTO t VALUES(10),(20),(30)")
        .unwrap();
    let mut engine = Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: db,
                read_only: false,
            },
        )
        .unwrap();
    let q = engine
        .execute(
            c,
            "UPDATE t SET x=x+1 RETURNING x".into(),
            Default::default(),
        )
        .unwrap();
    engine.fetch_page(q, PageSize::default()).unwrap();
    drop(wait(&mut engine, |e| matches!(e, Event::Page { .. })));
    let other = engine
        .execute(c, "SELECT 99".into(), Default::default())
        .unwrap();
    drop(wait(
        &mut engine,
        |e| matches!(e,Event::Schema{query,..} if *query==other),
    ));
    let path = dir.path().join("rows.jsonl");
    engine
        .start_export(q, path.clone(), ExportFormat::JsonLines)
        .unwrap();
    let event = wait(&mut engine, |e| {
        matches!(e, Event::ExportFinished { .. } | Event::ExportFailed { .. })
    });
    assert!(
        matches!(event, Event::ExportFinished { rows: 3, .. }),
        "{event:?}"
    );
    assert_eq!(
        std::fs::read_to_string(path).unwrap(),
        "{\"columns\":[{\"name\":\"x\",\"database_type\":\"INTEGER\",\"precision\":null,\"scale\":null,\"timezone\":null,\"nullable\":null}]}\n{\"row\":[11]}\n{\"row\":[21]}\n{\"row\":[31]}\n"
    );
    assert_eq!(
        observer
            .query_row("SELECT sum(x) FROM t", [], |row| row.get::<_, i64>(0))
            .unwrap(),
        63
    );
}

#[test]
fn deferred_text_and_blob_export_exceeds_memory_budget_without_materialization() {
    let dir = tempfile::tempdir().unwrap();
    let config = EngineConfig {
        page_memory: PageMemoryConfig {
            per_result_bytes: 1024 * 1024,
            global_bytes: 2 * 1024 * 1024,
        },
        ..Default::default()
    };
    let mut engine = Engine::new(config, vec![Arc::new(SqliteDriver)]).unwrap();
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
        .execute(
            c,
            "SELECT replace(hex(zeroblob(1048576)), '0', 'é') AS t, zeroblob(1048576) AS b".into(),
            Default::default(),
        )
        .unwrap();
    drop(wait(&mut engine, |e| matches!(e, Event::Schema { .. })));
    let path = dir.path().join("large.csv");
    engine
        .start_export(q, path.clone(), ExportFormat::Csv)
        .unwrap();
    let event = wait(&mut engine, |e| {
        matches!(e, Event::ExportFinished { .. } | Event::ExportFailed { .. })
    });
    assert!(
        matches!(event, Event::ExportFinished { rows: 1, .. }),
        "{event:?}"
    );
    let bytes = std::fs::read_to_string(path).unwrap();
    let expected = format!(
        "\"t\",\"b\"\r\n\"{}\",\"\\x{}\"\r\n",
        "é".repeat(2097152),
        "00".repeat(1048576)
    );
    assert!(
        bytes == expected,
        "large export content mismatch: actual length {}, expected {}",
        bytes.len(),
        expected.len()
    );
    assert!(engine.memory_usage().peak_bytes <= 1024 * 1024);
}

#[test]
fn cancelled_export_preserves_destination_and_retires_generation() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("rows.csv");
    std::fs::write(&path, "original").unwrap();
    let mut engine = Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let q=engine.execute(c,"WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<1000000000) SELECT x FROM c".into(),Default::default()).unwrap();
    drop(wait(&mut engine, |e| matches!(e, Event::Schema { .. })));
    let id = engine
        .start_export(q, path.clone(), ExportFormat::Csv)
        .unwrap();
    assert_eq!(
        engine.start_export(q, path.clone(), ExportFormat::Csv),
        Err(SubmitError::ResourceLimit)
    );
    drop(wait(&mut engine, |e| {
        matches!(e, Event::ExportProgress { .. })
    }));
    engine.cancel_export(id).unwrap();
    let event = wait(&mut engine, |e| {
        matches!(e, Event::ExportFinished { .. } | Event::ExportFailed { .. })
    });
    assert!(
        matches!(
            event,
            Event::ExportFailed {
                error: DriverError {
                    kind: ErrorKind::Cancelled,
                    ..
                },
                ..
            }
        ),
        "{event:?}"
    );
    assert_eq!(std::fs::read_to_string(path).unwrap(), "original");
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
    assert_eq!(engine.cancel_export(id), Err(SubmitError::StaleHandle));
    engine.fetch_page_at(q, 0, PageSize::default()).unwrap();
    assert!(
        matches!(wait(&mut engine,|e|matches!(e,Event::StoredPage{..})),Event::StoredPage{page,..} if page.rows[0][0]==Value::Integer(1))
    );
}

#[test]
fn incomplete_archived_result_fails_instead_of_publishing_a_prefix() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("rows.csv");
    std::fs::write(&path, "keep").unwrap();
    let mut engine = Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let q=engine.execute(c,"WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<251) SELECT x FROM c".into(),Default::default()).unwrap();
    engine.fetch_page(q, PageSize::new(100).unwrap()).unwrap();
    drop(wait(&mut engine, |e| matches!(e, Event::Page { .. })));
    let other = engine
        .execute(c, "SELECT 9".into(), Default::default())
        .unwrap();
    drop(wait(
        &mut engine,
        |e| matches!(e,Event::Schema{query,..} if *query==other),
    ));
    engine
        .start_export(q, path.clone(), ExportFormat::Csv)
        .unwrap();
    assert!(matches!(
        wait(&mut engine, |e| matches!(
            e,
            Event::ExportFailed { .. } | Event::ExportFinished { .. }
        )),
        Event::ExportFailed {
            error: DriverError {
                kind: ErrorKind::StaleHandle,
                ..
            },
            ..
        }
    ));
    assert_eq!(std::fs::read_to_string(path).unwrap(), "keep");
}

#[test]
fn query_release_interrupts_export_waiting_for_pinned_memory() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("rows.csv");
    let config = EngineConfig {
        page_memory: PageMemoryConfig {
            per_result_bytes: 1024 * 1024,
            global_bytes: 1024 * 1024,
        },
        ..Default::default()
    };
    let mut engine = Engine::new(config, vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let q=engine.execute(c,"WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<251) SELECT x FROM c".into(),Default::default()).unwrap();
    engine.fetch_page(q, PageSize::new(100).unwrap()).unwrap();
    let pinned = wait(&mut engine, |e| matches!(e, Event::Page { .. }));
    let id = engine
        .start_export(q, path.clone(), ExportFormat::Csv)
        .unwrap();
    engine.release_query(q).unwrap();
    assert!(
        matches!(wait(&mut engine,|e|matches!(e,Event::ExportFailed{..})),Event::ExportFailed{export,error:DriverError{kind:ErrorKind::Cancelled,..},..} if export==id)
    );
    assert!(!path.exists());
    drop(pinned);
}

#[test]
fn wide_inline_row_exports_within_the_shared_memory_envelope() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("wide.csv");
    let mut engine = Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let sql = format!(
        "SELECT {}",
        (0..64)
            .map(|i| format!("hex(zeroblob(8192)) AS c{i}"))
            .collect::<Vec<_>>()
            .join(",")
    );
    let q = engine.execute(c, sql, Default::default()).unwrap();
    drop(wait(&mut engine, |e| matches!(e, Event::Schema { .. })));
    engine
        .start_export(q, path.clone(), ExportFormat::Csv)
        .unwrap();
    let event = wait(&mut engine, |e| {
        matches!(e, Event::ExportFailed { .. } | Event::ExportFinished { .. })
    });
    assert!(
        matches!(event, Event::ExportFinished { rows: 1, .. }),
        "{event:?}"
    );
    let output = std::fs::read_to_string(path).unwrap();
    let mut lines = output.split("\r\n");
    assert_eq!(lines.next().unwrap().split(',').count(), 64);
    let fields: Vec<_> = lines.next().unwrap().split(',').collect();
    assert_eq!(fields.len(), 64);
    assert!(
        fields
            .iter()
            .all(|field| *field == format!("\"{}\"", "0".repeat(16384)))
    );
    assert!(engine.memory_usage().peak_bytes <= 64 * 1024 * 1024);
}

#[test]
fn cancelling_original_query_interrupts_live_export() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("rows.csv");
    let mut engine = Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let q=engine.execute(c,"WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<1000000000) SELECT x FROM c".into(),Default::default()).unwrap();
    drop(wait(&mut engine, |e| matches!(e, Event::Schema { .. })));
    engine
        .start_export(q, path.clone(), ExportFormat::Csv)
        .unwrap();
    drop(wait(&mut engine, |e| {
        matches!(e, Event::ExportProgress { .. })
    }));
    engine.cancel(q).unwrap();
    let event = wait(&mut engine, |e| {
        matches!(e, Event::ExportFailed { .. } | Event::ExportFinished { .. })
    });
    assert!(
        matches!(
            event,
            Event::ExportFailed {
                error: DriverError {
                    kind: ErrorKind::Cancelled,
                    ..
                },
                ..
            }
        ),
        "{event:?}"
    );
    assert!(!path.exists());
    let next = engine
        .execute(c, "SELECT 7".into(), Default::default())
        .unwrap();
    engine.fetch_page(next, PageSize::default()).unwrap();
    assert!(
        matches!(wait(&mut engine,|e|matches!(e,Event::Page{query,..} if *query==next)),Event::Page{page,..} if page.rows[0][0]==Value::Integer(7))
    );
}
#[test]
fn export_preserves_captured_query_page_size_in_archived_pages() {
    let directory = tempfile::tempdir().unwrap();
    let database = directory.path().join("page-size.sqlite");
    let observer = rusqlite::Connection::open(&database).unwrap();
    observer.execute_batch("CREATE TABLE values_once(id INTEGER PRIMARY KEY, value INTEGER); WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<351) INSERT INTO values_once SELECT x,0 FROM n;").unwrap();
    let mut engine = Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let connection = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: database,
                read_only: false,
            },
        )
        .unwrap();
    let page_size = PageSize::new(100).unwrap();
    let query = engine
        .execute(
            connection,
            "UPDATE values_once SET value=value+1 RETURNING id,value".into(),
            QueryOptions {
                page_size,
                ..Default::default()
            },
        )
        .unwrap();
    engine.fetch_page_at(query, 0, page_size).unwrap();
    assert!(
        matches!(wait(&mut engine, |e| matches!(e, Event::StoredPage { .. })), Event::StoredPage { first_row: 0, page, .. } if page.rows.len() == 100)
    );
    engine
        .start_export(
            query,
            directory.path().join("result.csv"),
            ExportFormat::Csv,
        )
        .unwrap();
    assert!(matches!(
        wait(&mut engine, |e| matches!(
            e,
            Event::ExportFinished { .. } | Event::ExportFailed { .. }
        )),
        Event::ExportFinished { rows: 351, .. }
    ));
    engine.fetch_page_at(query, 1, page_size).unwrap();
    let event = wait(&mut engine, |e| matches!(e, Event::StoredPage { .. }));
    assert!(
        matches!(event, Event::StoredPage { first_row: 100, page, .. } if page.rows.len() == 100),
        "export must retain the query's page size"
    );
    let (count, sum): (i64, i64) = observer
        .query_row("SELECT count(*),sum(value) FROM values_once", [], |row| {
            Ok((row.get(0)?, row.get(1)?))
        })
        .unwrap();
    assert_eq!((count, sum), (351, 351), "export or reread re-executed SQL");
}
