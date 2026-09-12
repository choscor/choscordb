use choscordb_driver_api::*;
use choscordb_driver_sqlite::SqliteDriver;
async fn connect() -> Box<dyn Connection> {
    SqliteDriver
        .connect(ConnectionOptions::Sqlite {
            path: ":memory:".into(),
            read_only: false,
        })
        .await
        .unwrap()
}
#[tokio::test]
async fn real_sqlite_preserves_null_empty_and_pages() {
    let mut c = connect().await;
    let mut r = c
        .execute("SELECT NULL, '', 42, x'00ff'", QueryOptions::default())
        .await
        .unwrap();
    let page = r.fetch_page(PageSize::default()).await.unwrap();
    assert_eq!(
        page.rows,
        vec![vec![
            Value::Null,
            Value::Text("".into()),
            Value::Integer(42),
            Value::Binary(vec![0, 255])
        ]]
    );
    assert!(!page.has_more);
}
async fn run(c: &mut Box<dyn Connection>, sql: &str, manual: bool) {
    let mut r = c
        .execute(
            sql,
            QueryOptions {
                auto_commit: !manual,
                ..Default::default()
            },
        )
        .await
        .unwrap();
    r.close().await.unwrap();
}
#[tokio::test]
async fn million_rows_stream_progressively() {
    let mut c = connect().await;
    let mut r=c.execute("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<1000000) SELECT x FROM n",QueryOptions::default()).await.unwrap();
    for i in 0..1000 {
        let p = r.fetch_page(PageSize::default()).await.unwrap();
        assert_eq!(p.index, i);
        assert_eq!(p.rows.len(), 1000);
        assert_eq!(p.rows[0][0], Value::Integer((i * 1000 + 1) as i64));
        assert_eq!(p.has_more, i < 999);
        assert!(p.estimated_bytes() < 200_000);
    }
}
#[tokio::test]
async fn commit_and_rollback_have_real_effects() {
    let mut c = connect().await;
    run(&mut c, "CREATE TABLE t(x)", false).await;
    run(&mut c, "INSERT INTO t VALUES(1)", true).await;
    c.rollback().await.unwrap();
    run(&mut c, "INSERT INTO t VALUES(2)", true).await;
    c.commit().await.unwrap();
    let mut r = c
        .execute("SELECT x FROM t", Default::default())
        .await
        .unwrap();
    assert_eq!(
        r.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(2)]]
    );
}
#[tokio::test]
async fn native_cancel_interrupts_active_work_and_connection_recovers() {
    let mut c = connect().await;
    let cancel = c.cancellation_handle();
    let mut r=c.execute("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<100000000) SELECT sum(x) FROM n",Default::default()).await.unwrap();
    let (page, ()) = tokio::join!(r.fetch_page(PageSize::default()), async {
        tokio::time::sleep(std::time::Duration::from_millis(10)).await;
        cancel.cancel().await.unwrap();
    });
    assert_eq!(page.unwrap_err().kind, ErrorKind::Cancelled);
    r.close().await.unwrap();
    let mut r = c.execute("SELECT 1", Default::default()).await.unwrap();
    assert_eq!(
        r.fetch_page(PageSize::default()).await.unwrap().rows[0][0],
        Value::Integer(1)
    );
}
#[tokio::test]
async fn timeout_is_distinguished_from_cancel() {
    let mut c = connect().await;
    let mut r=c.execute("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<100000000) SELECT sum(x) FROM n",QueryOptions{timeout:Some(std::time::Duration::from_millis(5)),..Default::default()}).await.unwrap();
    assert_eq!(
        r.fetch_page(PageSize::default()).await.unwrap_err().kind,
        ErrorKind::Timeout
    );
}
#[tokio::test]
async fn metadata_handles_adversarial_identifiers_and_loads_lazily() {
    let mut c = connect().await;
    run(
        &mut c,
        "CREATE TABLE \"x'; DROP TABLE users;--\" (\"a\" INTEGER PRIMARY KEY, \"b\" TEXT UNIQUE)",
        false,
    )
    .await;
    let roots = c.load_metadata(None).await.unwrap();
    assert_eq!(roots.len(), 1);
    assert_eq!(roots[0].kind, ObjectKind::Database);
    let tables = c.load_metadata(Some(roots[0].id.clone())).await.unwrap();
    assert_eq!(tables.len(), 1);
    assert_eq!(tables[0].name, "x'; DROP TABLE users;--");
    let children = c.load_metadata(Some(tables[0].id.clone())).await.unwrap();
    assert!(children.iter().any(|c| c.kind == ObjectKind::PrimaryKey));
    assert!(children.iter().any(|c| c.kind == ObjectKind::UniqueKey));
    assert!(
        c.object_ddl(&tables[0].id)
            .await
            .unwrap()
            .contains("CREATE TABLE")
    );
}
#[tokio::test]
async fn large_blob_is_deferred_and_loaded_on_request() {
    let mut c = connect().await;
    let mut r = c
        .execute("SELECT zeroblob(1000000)", Default::default())
        .await
        .unwrap();
    let page = r.fetch_page(PageSize::default()).await.unwrap();
    assert!(page.estimated_bytes() < 1000);
    let Value::Deferred {
        handle,
        byte_length,
        ..
    } = page.rows[0][0]
    else {
        panic!("must defer blob")
    };
    assert_eq!(byte_length, 1000000);
    assert_eq!(
        r.load_value(handle).await.unwrap(),
        Value::Binary(vec![0; 1000000])
    );
}
#[tokio::test]
async fn stale_cancel_cannot_interrupt_a_subsequent_query() {
    let mut c = connect().await;
    let old = c.cancellation_handle();
    let mut first = c.execute("SELECT 1", Default::default()).await.unwrap();
    first.close().await.unwrap();
    let mut second = c.execute("SELECT 2", Default::default()).await.unwrap();
    old.cancel().await.unwrap();
    assert_eq!(
        second.fetch_page(PageSize::default()).await.unwrap().rows[0][0],
        Value::Integer(2)
    );
}

#[tokio::test]
async fn sqlite_errors_preserve_vendor_codes_without_log_leaks() {
    let mut c = connect().await;
    let error = match c
        .execute(
            "SELECT secret_column FROM no_such_table",
            Default::default(),
        )
        .await
    {
        Ok(_) => panic!("expected missing table"),
        Err(e) => e,
    };
    assert!(error.vendor_code.is_some());
    assert!(!format!("{error:?}").contains("no_such_table"));
}

#[tokio::test]
async fn additional_statements_are_rejected_without_side_effects() {
    let mut c = connect().await;
    run(&mut c, "CREATE TABLE t(x)", false).await;
    for sql in [
        "INSERT INTO t VALUES(1); INSERT INTO t VALUES(2)",
        "SELECT ';'; SELECT 2",
    ] {
        let e = match c.execute(sql, Default::default()).await {
            Ok(_) => panic!("multiple statements accepted"),
            Err(e) => e,
        };
        assert_eq!(e.kind, ErrorKind::Unsupported);
    }
    let mut r = c
        .execute(
            "SELECT count(*) FROM t; -- trailing comment\n",
            Default::default(),
        )
        .await
        .unwrap();
    assert_eq!(
        r.fetch_page(PageSize::default()).await.unwrap().rows[0][0],
        Value::Integer(0)
    );
}
#[tokio::test]
async fn timed_write_reports_timeout_and_rolls_back_statement() {
    let mut c = connect().await;
    run(&mut c, "CREATE TABLE t(x)", false).await;
    let e=match c.execute("INSERT INTO t WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<100000000) SELECT x FROM n",QueryOptions{timeout:Some(std::time::Duration::from_millis(5)),..Default::default()}).await {Ok(_)=>panic!("write should time out"),Err(e)=>e};
    assert_eq!(e.kind, ErrorKind::Timeout);
    let mut r = c
        .execute("SELECT count(*) FROM t", Default::default())
        .await
        .unwrap();
    assert_eq!(
        r.fetch_page(PageSize::default()).await.unwrap().rows[0][0],
        Value::Integer(0)
    );
}

#[tokio::test]
async fn dropping_connection_interrupts_retained_cursor_worker() {
    let mut c = connect().await;
    let mut cursor=c.execute("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<100000000) SELECT sum(x) FROM n",Default::default()).await.unwrap();
    let work = async {
        tokio::join!(cursor.fetch_page(PageSize::default()), async {
            tokio::time::sleep(std::time::Duration::from_millis(10)).await;
            drop(c);
        })
    };
    let (result, ()) = tokio::time::timeout(std::time::Duration::from_secs(1), work)
        .await
        .expect("worker must settle after connection drop");
    assert!(result.is_err());
    assert!(cursor.fetch_page(PageSize::default()).await.is_err());
}

#[tokio::test]
async fn cancellation_before_worker_starts_is_remembered_and_does_not_write() {
    let mut c = connect().await;
    let previous = c.cancellation_handle();
    run(&mut c, "CREATE TABLE t(value INTEGER)", true).await;
    let pending = c.cancellation_handle();
    pending.cancel().await.unwrap();
    // A delayed cancellation of the previous generation must not overwrite the
    // remembered cancellation of the queued next generation.
    previous.cancel().await.unwrap();
    let error = match c
        .execute("INSERT INTO t VALUES (1)", QueryOptions::default())
        .await
    {
        Ok(_) => panic!("pre-cancelled write must never execute"),
        Err(error) => error,
    };
    assert_eq!(error.kind, ErrorKind::Cancelled);
    let mut result = c
        .execute("SELECT count(*) FROM t", QueryOptions::default())
        .await
        .unwrap();
    pending.cancel().await.unwrap();
    assert_eq!(
        result.fetch_page(PageSize::default()).await.unwrap().rows[0][0],
        Value::Integer(0)
    );
}

#[tokio::test]
async fn primary_key_nullability_matches_sqlite_table_semantics() {
    let mut c = connect().await;
    for sql in [
        "CREATE TABLE nullable_pk(id TEXT PRIMARY KEY)",
        "CREATE TABLE rowid_pk(id INTEGER PRIMARY KEY)",
        "CREATE TABLE required_pk(id TEXT PRIMARY KEY) WITHOUT ROWID",
        "CREATE TABLE descending_pk(id INTEGER PRIMARY KEY DESC)",
        "CREATE TABLE strict_pk(id TEXT PRIMARY KEY) STRICT",
        "CREATE TABLE composite_pk(id INTEGER, other TEXT, PRIMARY KEY(id,other))",
    ] {
        run(&mut c, sql, true).await;
    }
    run(&mut c, "INSERT INTO nullable_pk VALUES(NULL)", true).await;
    for (table, nullable) in [
        ("nullable_pk", true),
        ("rowid_pk", false),
        ("required_pk", false),
        ("descending_pk", true),
        ("strict_pk", false),
        ("composite_pk", true),
    ] {
        let objects = c
            .load_metadata(Some(ObjectId(format!("[\"main\",\"{table}\"]"))))
            .await
            .unwrap();
        let column = objects
            .iter()
            .find_map(|object| object.column.as_ref())
            .unwrap();
        assert_eq!(column.nullable, Some(nullable), "{table}");
    }
}

#[tokio::test]
async fn bounded_pages_preserve_every_row_and_reject_tiny_budget_without_advancing() {
    let mut c = connect().await;
    let mut r = c.execute("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<31) SELECT x, printf('%0500d',x), zeroblob(500) FROM n", QueryOptions::default()).await.unwrap();
    assert_eq!(
        r.fetch_page_bounded(PageSize::default(), 1)
            .await
            .unwrap_err()
            .kind,
        ErrorKind::ResourceLimit
    );
    let mut seen = 0;
    loop {
        let page = r
            .fetch_page_bounded(PageSize::default(), 1024)
            .await
            .unwrap();
        assert!(page.estimated_bytes() <= 1024);
        assert!(!page.rows.is_empty());
        for row in page.rows {
            seen += 1;
            assert_eq!(row[0], Value::Integer(seen));
            for (i, value) in row.into_iter().enumerate().skip(1) {
                let value = match value {
                    Value::Deferred { handle, .. } => r.load_value(handle).await.unwrap(),
                    value => value,
                };
                if i == 1 {
                    assert_eq!(value, Value::Text(format!("{seen:0500}")));
                } else {
                    assert_eq!(value, Value::Binary(vec![0; 500]));
                }
            }
        }
        if !page.has_more {
            break;
        }
    }
    assert_eq!(seen, 31);
}

#[tokio::test]
async fn bounded_wide_rows_defer_payload_and_keep_lookahead_on_budget_error() {
    let mut c = connect().await;
    let sql = format!(
        "SELECT {} UNION ALL SELECT {}",
        vec!["printf('%01000d',42)"; 80].join(","),
        vec!["printf('%01000d',43)"; 80].join(",")
    );
    let mut r = c.execute(&sql, QueryOptions::default()).await.unwrap();
    let first = r
        .fetch_page_bounded(PageSize::default(), 4096)
        .await
        .unwrap();
    assert_eq!(first.rows.len(), 1);
    assert!(first.has_more);
    assert!(first.estimated_bytes() <= 4096);
    assert!(
        first.rows[0]
            .iter()
            .all(|v| matches!(v, Value::Deferred { .. }))
    );
    assert_eq!(
        r.fetch_page_bounded(PageSize::default(), 256)
            .await
            .unwrap_err()
            .kind,
        ErrorKind::ResourceLimit
    );
    let second = r
        .fetch_page_bounded(PageSize::default(), 4096)
        .await
        .unwrap();
    assert_eq!(second.index, 1);
    assert_eq!(second.rows.len(), 1);
    assert!(!second.has_more);
    let Value::Deferred { handle, .. } = second.rows[0][79] else {
        panic!("expected deferred text")
    };
    assert_eq!(
        r.load_value(handle).await.unwrap(),
        Value::Text(format!("{:01000}", 43))
    );
}

#[tokio::test]
async fn bounded_schema_rejection_precedes_returning_write_and_connection_recovers() {
    let mut c = connect().await;
    run(&mut c, "CREATE TABLE bounded_schema(x)", false).await;
    let sql = format!(
        "INSERT INTO bounded_schema VALUES(1) RETURNING x AS \"{}\"",
        "a".repeat(4096)
    );
    match c.execute_bounded(&sql, QueryOptions::default(), 256).await {
        Err(error) => assert_eq!(error.kind, ErrorKind::ResourceLimit),
        Ok(_) => panic!("oversized schema accepted"),
    }
    let mut cursor = c
        .execute(
            "SELECT count(*) FROM bounded_schema",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    let page = cursor.fetch_page(PageSize::default()).await.unwrap();
    assert_eq!(page.rows, vec![vec![Value::Integer(0)]]);
    assert!(!page.has_more);
    assert!(
        cursor
            .retained_bytes_after_completion()
            .is_some_and(|bytes| bytes < 128 * 1024)
    );
}

#[tokio::test]
async fn deferred_reader_survives_cursor_and_connection_and_bounds_chunks() {
    let mut c = connect().await;
    let mut r = c
        .execute(
            "SELECT replace(hex(zeroblob(40000)), '0', '界'), zeroblob(200000)",
            Default::default(),
        )
        .await
        .unwrap();
    let reader = r
        .deferred_reader()
        .expect("SQLite supplies independent reader");
    let page = r.fetch_page(PageSize::default()).await.unwrap();
    r.close().await.unwrap();
    drop(r);
    let mut next = c
        .execute("SELECT zeroblob(300000)", Default::default())
        .await
        .unwrap();
    next.fetch_page(PageSize::default()).await.unwrap();
    next.close().await.unwrap();
    c.close().await.unwrap();
    tokio::task::spawn_blocking(move || {
        for (column, expected) in ["界".repeat(80000).into_bytes(), vec![0; 200000]]
            .into_iter()
            .enumerate()
        {
            let Value::Deferred { handle, .. } = page.rows[0][column] else {
                panic!("deferred")
            };
            let mut bytes = Vec::new();
            while bytes.len() < expected.len() {
                let chunk = reader.read_chunk(handle, bytes.len() as u64, 997).unwrap();
                assert!(chunk.bytes.len() <= 997);
                assert_eq!(chunk.total_bytes, expected.len() as u64);
                assert_eq!(
                    chunk.kind,
                    if column == 0 {
                        DeferredKind::Text
                    } else {
                        DeferredKind::Binary
                    }
                );
                bytes.extend(chunk.bytes);
            }
            assert_eq!(bytes, expected);
            assert!(
                reader
                    .read_chunk(handle, expected.len() as u64, 1)
                    .unwrap()
                    .bytes
                    .is_empty()
            );
            assert!(
                reader
                    .read_chunk(handle, expected.len() as u64 + 1, 1)
                    .is_err()
            );
            assert!(reader.read_chunk(handle, 0, 0).is_err());
            assert!(
                reader
                    .read_chunk(handle, 0, MAX_VALUE_CHUNK_BYTES + 1)
                    .is_err()
            );
            assert!(
                reader
                    .read_chunk(
                        Handle {
                            generation: handle.generation + 1,
                            ..handle
                        },
                        0,
                        1
                    )
                    .is_err()
            );
            assert!(
                reader
                    .read_chunk(
                        Handle {
                            slot: handle.slot + 1,
                            ..handle
                        },
                        0,
                        1
                    )
                    .is_err()
            );
        }
    })
    .await
    .unwrap();
}

#[tokio::test]
async fn execution_summary_reports_native_transaction_state() {
    let mut c = connect().await;
    for (sql, manual, expected) in [
        ("CREATE TABLE state_test(x)", false, false),
        ("BEGIN", false, true),
        ("INSERT INTO state_test VALUES(1)", false, true),
        ("SAVEPOINT nested", false, true),
        ("RELEASE nested", false, true),
        ("COMMIT", false, false),
        ("SAVEPOINT outermost", false, true),
        ("RELEASE outermost", false, false),
        ("BEGIN", false, true),
        ("ROLLBACK", false, false),
        ("INSERT INTO state_test VALUES(2)", true, true),
        ("SAVEPOINT manual_nested", true, true),
        ("ROLLBACK TO manual_nested", true, true),
        ("COMMIT", true, false),
        ("INSERT INTO state_test VALUES(3)", true, true),
        ("ROLLBACK", true, false),
    ] {
        let mut cursor = c
            .execute(
                sql,
                QueryOptions {
                    auto_commit: !manual,
                    ..Default::default()
                },
            )
            .await
            .unwrap();
        assert_eq!(cursor.summary().transaction_active, Some(expected), "{sql}");
        cursor.close().await.unwrap();
    }
}
