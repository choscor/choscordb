use choscordb_core::*;
use choscordb_driver_api::*;
use choscordb_driver_sqlite::SqliteDriver;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};

#[test]
fn dropping_engine_interrupts_real_sqlite_write_and_releases_database_lock() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("shutdown.sqlite");
    let observer = rusqlite::Connection::open(&path).unwrap();
    observer
        .execute_batch("CREATE TABLE t(value INTEGER)")
        .unwrap();
    observer.busy_timeout(Duration::ZERO).unwrap();
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path,
                read_only: false,
            },
        )
        .unwrap();
    engine.execute(c, "INSERT INTO t SELECT sum(x) FROM (WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<1000000000) SELECT x FROM c)".into(), QueryOptions { auto_commit: false, ..Default::default() }).unwrap();
    let deadline = Instant::now() + Duration::from_secs(3);
    loop {
        if observer.execute_batch("BEGIN IMMEDIATE").is_err() {
            break;
        }
        observer.execute_batch("ROLLBACK").unwrap();
        assert!(
            Instant::now() < deadline,
            "SQLite write never acquired its transaction lock"
        );
        std::thread::sleep(Duration::from_millis(1));
    }
    let start = Instant::now();
    drop(engine);
    assert!(start.elapsed() < Duration::from_millis(100));
    loop {
        if observer.execute_batch("BEGIN IMMEDIATE").is_ok() {
            break;
        }
        assert!(
            start.elapsed() < Duration::from_secs(3),
            "background SQLite write survived engine drop"
        );
        std::thread::sleep(Duration::from_millis(1));
    }
    observer.execute_batch("ROLLBACK").unwrap();
    let count: i64 = observer
        .query_row("SELECT count(*) FROM t", [], |row| row.get(0))
        .unwrap();
    assert_eq!(count, 0, "shutdown must not commit an interrupted write");
}

#[test]
fn metadata_and_ddl_do_not_destroy_a_paged_sqlite_result() {
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let q = engine.execute(c, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<250) SELECT x FROM c".into(), QueryOptions::default()).unwrap();
    engine.fetch_page(q, PageSize::new(100).unwrap()).unwrap();
    let deadline = Instant::now() + Duration::from_secs(3);
    let mut wait = |predicate: &dyn Fn(&Event) -> bool| -> Event {
        loop {
            if let Some(event) = engine.try_event()
                && predicate(&event)
            {
                return event;
            }
            assert!(Instant::now() < deadline, "event timed out");
            std::thread::sleep(Duration::from_millis(1));
        }
    };
    assert!(
        matches!(wait(&|e| matches!(e, Event::Page {..})), Event::Page { page, .. } if page.rows[0][0] == Value::Integer(1))
    );
    engine.load_metadata_request(c, None, 17).unwrap();
    engine
        .object_ddl(c, ObjectId("[\"main\",\"missing\"]".into()))
        .unwrap();
    engine.fetch_page(q, PageSize::new(100).unwrap()).unwrap();
    loop {
        if let Some(event) = engine.try_event() {
            match event {
                Event::QueryFailed { error, .. } => panic!("metadata destroyed result: {error}"),
                Event::Page { page, .. } => {
                    assert_eq!(page.rows[0][0], Value::Integer(101));
                    break;
                }
                _ => (),
            }
        }
        assert!(Instant::now() < deadline, "second page timed out");
        std::thread::sleep(Duration::from_millis(1));
    }
}

fn result_event(engine: &mut Engine) -> Event {
    let deadline = Instant::now() + Duration::from_secs(3);
    loop {
        if let Some(event) = engine.try_event()
            && matches!(event, Event::StoredPage { .. } | Event::QueryFailed { .. })
        {
            return event;
        }
        assert!(Instant::now() < deadline, "result timed out");
        std::thread::sleep(Duration::from_millis(1));
    }
}

#[test]
fn indexed_pages_revisit_original_cursor_with_actual_row_offsets() {
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let q = engine.execute(c, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<700) SELECT x FROM c".into(), QueryOptions::default()).unwrap();
    for (index, size, offset, value) in [
        (0, 100, 0, 1),
        (1, 200, 100, 101),
        (0, 100, 0, 1),
        (2, 100, 300, 301),
    ] {
        engine
            .fetch_page_at(q, index, PageSize::new(size).unwrap())
            .unwrap();
        match result_event(&mut engine) {
            Event::StoredPage {
                first_row, page, ..
            } => {
                assert_eq!(first_row, offset);
                assert_eq!(page.rows[0][0], Value::Integer(value));
                assert_eq!(page.index, index);
            }
            other => panic!("unexpected {other:?}"),
        }
    }
}

#[test]
fn stored_result_survives_new_writes_and_transaction_without_replaying_sql() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("original.sqlite");
    let observer = rusqlite::Connection::open(&path).unwrap();
    observer
        .execute_batch("CREATE TABLE t(x); INSERT INTO t VALUES (1),(2),(3)")
        .unwrap();
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path,
                read_only: false,
            },
        )
        .unwrap();
    let q = engine
        .execute(
            c,
            "UPDATE t SET x=x+10 RETURNING x".into(),
            QueryOptions::default(),
        )
        .unwrap();
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    assert!(
        matches!(result_event(&mut engine), Event::StoredPage { page, .. } if page.rows[0][0] == Value::Integer(11))
    );
    let next = engine
        .execute(c, "UPDATE t SET x=x+100".into(), QueryOptions::default())
        .unwrap();
    engine
        .fetch_page_at(next, 0, PageSize::new(100).unwrap())
        .unwrap();
    assert!(matches!(
        result_event(&mut engine),
        Event::StoredPage { .. }
    ));
    engine.commit(c).unwrap();
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    assert!(
        matches!(result_event(&mut engine), Event::StoredPage { page, .. } if page.rows[0][0] == Value::Integer(11))
    );
    assert_eq!(
        observer
            .query_row("SELECT min(x) FROM t", [], |r| r.get::<_, i64>(0))
            .unwrap(),
        111
    );
    engine
        .fetch_page_at(q, 1, PageSize::new(100).unwrap())
        .unwrap();
    assert!(
        matches!(result_event(&mut engine), Event::QueryFailed { error, .. } if error.kind == ErrorKind::StaleHandle)
    );
    engine.release_query(q).unwrap();
    assert_eq!(
        engine.fetch_page_at(q, 0, PageSize::new(100).unwrap()),
        Err(SubmitError::StaleHandle)
    );
}

#[test]
fn skipped_ordinal_does_not_advance_original_cursor() {
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
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
        .fetch_page_at(q, 1, PageSize::new(100).unwrap())
        .unwrap();
    assert!(
        matches!(result_event(&mut engine), Event::QueryFailed { error, .. } if error.kind == ErrorKind::InvalidInput)
    );
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    assert!(
        matches!(result_event(&mut engine), Event::StoredPage { page, .. } if page.rows[0][0] == Value::Integer(73))
    );
}

#[test]
fn archived_partial_result_exposes_only_stored_next_pages() {
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let q = engine.execute(c, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<700) SELECT x FROM c".into(), QueryOptions::default()).unwrap();
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    assert!(matches!(result_event(&mut engine), Event::StoredPage { page, .. } if page.has_more));
    engine.commit(c).unwrap();
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    assert!(matches!(result_event(&mut engine), Event::StoredPage { page, .. } if !page.has_more));
}

#[test]
fn releasing_result_removes_its_disk_files() {
    let dir = tempfile::tempdir().unwrap();
    let config = EngineConfig {
        result_store_directory: Some(dir.path().into()),
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
        .execute(c, "SELECT 73".into(), QueryOptions::default())
        .unwrap();
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    assert!(matches!(
        result_event(&mut engine),
        Event::StoredPage { .. }
    ));
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
    engine.release_query(q).unwrap();
    let deadline = Instant::now() + Duration::from_secs(3);
    while std::fs::read_dir(dir.path()).unwrap().count() != 0 {
        assert!(Instant::now() < deadline, "result files leaked");
        std::thread::sleep(Duration::from_millis(1));
    }
}

#[test]
fn storage_creation_failure_does_not_reexecute_a_completed_write() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("write.sqlite");
    let observer = rusqlite::Connection::open(&path).unwrap();
    observer.execute_batch("CREATE TABLE t(x)").unwrap();
    let config = EngineConfig {
        result_store_directory: Some(dir.path().join("absent")),
        ..Default::default()
    };
    let mut engine = Engine::new(config, vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path,
                read_only: false,
            },
        )
        .unwrap();
    engine
        .execute(
            c,
            "INSERT INTO t VALUES (73)".into(),
            QueryOptions::default(),
        )
        .unwrap();
    match result_event(&mut engine) {
        Event::QueryFailed { error, .. } => {
            assert_eq!(error.kind, ErrorKind::Io);
            assert!(error.message.contains("already executed"));
            assert!(error.message.contains("do not retry"));
        }
        other => panic!("unexpected {other:?}"),
    }
    assert_eq!(
        observer
            .query_row("SELECT count(*) FROM t", [], |r| r.get::<_, i64>(0))
            .unwrap(),
        1
    );
}

#[test]
fn disk_limit_stops_fetch_and_preserves_already_persisted_pages() {
    let mut config = EngineConfig::default();
    config.result_store.max_disk_bytes = 1024;
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
    let q = engine.execute(c, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<700) SELECT x FROM c".into(), QueryOptions::default()).unwrap();
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    assert!(matches!(
        result_event(&mut engine),
        Event::StoredPage { .. }
    ));
    engine
        .fetch_page_at(q, 1, PageSize::new(100).unwrap())
        .unwrap();
    assert!(
        matches!(result_event(&mut engine), Event::QueryFailed { error, .. } if error.kind == ErrorKind::ResourceLimit)
    );
    engine
        .fetch_page_at(q, 0, PageSize::new(100).unwrap())
        .unwrap();
    assert!(
        matches!(result_event(&mut engine), Event::StoredPage { page, .. } if page.rows[0][0] == Value::Integer(1) && !page.has_more)
    );
    engine
        .fetch_page_at(q, 1, PageSize::new(100).unwrap())
        .unwrap();
    assert!(
        matches!(result_event(&mut engine), Event::QueryFailed { error, .. } if error.kind == ErrorKind::StaleHandle)
    );
}

#[test]
fn fetching_past_eof_preserves_deferred_values_and_stored_pages() {
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
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
            "SELECT zeroblob(1000000)".into(),
            QueryOptions::default(),
        )
        .unwrap();
    engine.fetch_page_at(q, 0, PageSize::default()).unwrap();
    let handle = match result_event(&mut engine) {
        Event::StoredPage { page, .. } => match page.rows[0][0] {
            Value::Deferred { handle, .. } => handle,
            _ => panic!("expected deferred BLOB"),
        },
        other => panic!("unexpected {other:?}"),
    };
    for indexed in [true, false] {
        if indexed {
            engine.fetch_page_at(q, 1, PageSize::default()).unwrap();
        } else {
            engine.fetch_page(q, PageSize::default()).unwrap();
        }
        assert!(
            matches!(result_event(&mut engine), Event::QueryFailed {error,..} if error.kind == ErrorKind::InvalidInput)
        );
        engine.load_value(q, handle).unwrap();
        let deadline = Instant::now() + Duration::from_secs(3);
        loop {
            match engine.try_event() {
                Some(Event::Value { value, .. }) => {
                    assert_eq!(value, Value::Binary(vec![0; 1000000]));
                    break;
                }
                Some(Event::QueryFailed { error, .. }) => panic!("deferred value lost: {error:?}"),
                _ => (),
            }
            assert!(Instant::now() < deadline, "value timed out");
            std::thread::sleep(Duration::from_millis(1));
        }
        engine.fetch_page_at(q, 0, PageSize::default()).unwrap();
        assert!(
            matches!(result_event(&mut engine), Event::StoredPage {page,..} if !page.has_more && matches!(page.rows[0][0],Value::Deferred {..}))
        );
    }
}

#[test]
fn million_rows_traverse_original_cursor_then_revisit_disk_pages() {
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let q = engine.execute(c, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<1000000) SELECT x FROM c".into(), QueryOptions::default()).unwrap();
    for index in (0..1000).chain([0, 999]) {
        engine
            .fetch_page_at(q, index, PageSize::new(1000).unwrap())
            .unwrap();
        match result_event(&mut engine) {
            Event::StoredPage {
                first_row, page, ..
            } => {
                assert_eq!(page.index, index);
                assert_eq!(first_row, index * 1000);
                assert_eq!(page.rows.len(), 1000);
                assert_eq!(page.has_more, index != 999);
                for (row, values) in page.rows.iter().enumerate() {
                    assert_eq!(
                        values.as_slice(),
                        &[Value::Integer((index * 1000 + row as u64 + 1) as i64)]
                    );
                }
            }
            other => panic!("unexpected {other:?}"),
        }
    }
}

#[test]
fn deferred_chunks_survive_new_sql_and_commit_and_remain_budgeted() {
    fn wait(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) -> Event {
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            if let Some(event) = engine.try_event() {
                assert!(
                    !matches!(event, Event::QueryFailed { .. }),
                    "viewer must not fail the query"
                );
                if predicate(&event) {
                    return event;
                }
            }
            assert!(Instant::now() < deadline);
            std::thread::sleep(Duration::from_millis(1));
        }
    }
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
            "SELECT zeroblob(200000)".into(),
            QueryOptions {
                auto_commit: false,
                ..Default::default()
            },
        )
        .unwrap();
    engine.fetch_page(q, PageSize::new(100).unwrap()).unwrap();
    let Event::Page { page, .. } = wait(&mut engine, |e| matches!(e, Event::Page { .. })) else {
        unreachable!()
    };
    let Value::Deferred { handle, .. } = page.rows[0][0] else {
        panic!("large value must defer")
    };
    engine.commit(c).unwrap();
    wait(&mut engine, |e| {
        matches!(e, Event::TransactionFinished { .. })
    });
    let newer = engine
        .execute(c, "SELECT 2".into(), QueryOptions::default())
        .unwrap();
    wait(
        &mut engine,
        |e| matches!(e, Event::Schema { query, .. } if *query == newer),
    );
    engine
        .load_value_chunk(q, handle, 0, MAX_VALUE_CHUNK_BYTES)
        .unwrap();
    let event = wait(&mut engine, |e| matches!(e, Event::ValueChunk { .. }));
    let Event::ValueChunk { chunk, lease, .. } = event else {
        unreachable!()
    };
    assert_eq!(chunk.total_bytes, 200000);
    assert_eq!(chunk.bytes.len(), 1024 * 1024 / 32 - 64);
    assert!(chunk.bytes.iter().all(|b| *b == 0));
    let used = engine.memory_usage().used_bytes;
    let reserved = lease.reserved_bytes();
    drop(lease);
    assert_eq!(engine.memory_usage().used_bytes, used - reserved);
    for (query, offset, size) in [
        (newer, 0, 12),
        (q, 200001, 12),
        (q, 0, 0),
        (q, 0, MAX_VALUE_CHUNK_BYTES + 1),
    ] {
        engine
            .load_value_chunk(query, handle, offset, size)
            .unwrap();
        wait(
            &mut engine,
            |e| matches!(e, Event::ValueChunkFailed { query: got, .. } if *got == query),
        );
    }
    engine.load_value_chunk(q, handle, 199999, 12).unwrap();
    let Event::ValueChunk { chunk, .. } =
        wait(&mut engine, |e| matches!(e, Event::ValueChunk { .. }))
    else {
        unreachable!()
    };
    assert_eq!(chunk.bytes, vec![0]);
    engine.release_query(q).unwrap();
    assert_eq!(
        engine.load_value_chunk(q, handle, 0, 12),
        Err(SubmitError::StaleHandle)
    );
}

#[test]
fn object_result_has_independent_pages_export_and_sql_cursor() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("object.sqlite");
    let fixture = rusqlite::Connection::open(&path).unwrap();
    fixture.execute_batch("CREATE TABLE data(x); INSERT INTO data WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<250) SELECT x*10 FROM n").unwrap();
    drop(fixture);
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path,
                read_only: false,
            },
        )
        .unwrap();
    let sql=engine.execute(c,"WITH RECURSIVE n(x) AS (VALUES(101) UNION ALL SELECT x+1 FROM n WHERE x<350) SELECT x FROM n".into(),QueryOptions::default()).unwrap();
    let size = PageSize::new(100).unwrap();
    engine.fetch_page_at(sql, 0, size).unwrap();
    assert!(
        matches!(result_event(&mut engine),Event::StoredPage{page,..} if page.rows[0][0]==Value::Integer(101))
    );
    let object = engine
        .open_object_data(
            c,
            ObjectId(r#"["main","data"]"#.into()),
            QueryOptions {
                page_size: size,
                ..Default::default()
            },
        )
        .unwrap();
    engine.fetch_page_at(object, 0, size).unwrap();
    assert!(
        matches!(result_event(&mut engine),Event::StoredPage{query,page,..} if query==object && page.rows.len()==100 && page.rows[0][0]==Value::Integer(10))
    );
    engine.fetch_page_at(sql, 1, size).unwrap();
    assert!(
        matches!(result_event(&mut engine),Event::StoredPage{query,page,..} if query==sql && page.rows[0][0]==Value::Integer(201))
    );
    let destination = dir.path().join("object.csv");
    engine
        .start_export(object, destination.clone(), ExportFormat::Csv)
        .unwrap();
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(event) = engine.try_event() {
            match event {
                Event::ExportFinished { .. } => break,
                Event::ExportFailed { error, .. } => panic!("{error}"),
                _ => (),
            }
        }
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(1));
    }
    let bytes = std::fs::read_to_string(destination).unwrap();
    assert!(bytes.contains("2500"));
    assert_eq!(bytes.lines().count(), 251);
    engine.fetch_page_at(sql, 2, size).unwrap();
    assert!(
        matches!(result_event(&mut engine),Event::StoredPage{query,page,..} if query==sql && page.rows[0][0]==Value::Integer(301))
    );
}

#[test]
fn cancelling_object_read_preserves_sql_and_new_object_can_read_deferred_values() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("object-cancel.sqlite");
    let fixture = rusqlite::Connection::open(&path).unwrap();
    fixture.execute_batch("CREATE VIEW slow AS WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<1000000000) SELECT sum(x) FROM n; CREATE TABLE data(value); INSERT INTO data VALUES(printf('%070000d',7));").unwrap();
    drop(fixture);
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let c = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path,
                read_only: false,
            },
        )
        .unwrap();
    let sql=engine.execute(c,"WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<250) SELECT x FROM n".into(),QueryOptions::default()).unwrap();
    let size = PageSize::new(100).unwrap();
    engine.fetch_page_at(sql, 0, size).unwrap();
    assert!(matches!(
        result_event(&mut engine),
        Event::StoredPage { .. }
    ));
    let slow = engine
        .open_object_data(
            c,
            ObjectId(r#"["main","slow"]"#.into()),
            QueryOptions::default(),
        )
        .unwrap();
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(Event::Schema { query, .. }) = engine.try_event()
            && query == slow
        {
            break;
        }
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(1));
    }
    engine.fetch_page_at(slow, 0, size).unwrap();
    std::thread::sleep(Duration::from_millis(30));
    engine.cancel(slow).unwrap();
    assert!(
        matches!(result_event(&mut engine),Event::QueryFailed{query,error} if query==slow && error.kind==ErrorKind::Cancelled)
    );
    engine.fetch_page_at(sql, 1, size).unwrap();
    assert!(
        matches!(result_event(&mut engine),Event::StoredPage{query,page,..} if query==sql && page.rows[0][0]==Value::Integer(101))
    );
    let object = engine
        .open_object_data(
            c,
            ObjectId(r#"["main","data"]"#.into()),
            QueryOptions::default(),
        )
        .unwrap();
    engine.fetch_page_at(object, 0, size).unwrap();
    let Event::StoredPage { page, .. } = result_event(&mut engine) else {
        panic!("missing object page")
    };
    let Value::Deferred {
        handle,
        byte_length,
        ..
    } = page.rows[0][0]
    else {
        panic!("value must be deferred")
    };
    assert_eq!(byte_length, 70000);
    engine.load_value_chunk(object, handle, 69995, 5).unwrap();
    loop {
        if let Some(Event::ValueChunk { query, chunk, .. }) = engine.try_event() {
            assert_eq!(query, object);
            assert_eq!(chunk.bytes, b"00007");
            break;
        }
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(1));
    }
    engine.release_query(object).unwrap();
    assert_eq!(
        engine.load_value_chunk(object, handle, 0, 5),
        Err(SubmitError::StaleHandle)
    );
}
