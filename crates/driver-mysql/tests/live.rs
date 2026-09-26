//! Run with CHOSCORDB_MYSQL_TEST=1 against the disposable MySQL test server.
use choscordb_driver_api::*;
use choscordb_driver_mysql::MysqlDriver;
async fn connect() -> Box<dyn Connection> {
    MysqlDriver
        .connect(ConnectionOptions::Mysql {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: None,
            proxy_secret: None,
            host: "127.0.0.1".into(),
            port: std::env::var("CHOSCORDB_MYSQL_PORT")
                .unwrap_or_else(|_| "33306".into())
                .parse()
                .unwrap(),
            database: "choscordb_test".into(),
            user: "root".into(),
            password: Some(Secret::new("choscordb-test-password")),
            ssh_secret: None,
            tls: TlsMode::Disable,
            tls_identity: None,
            root_certificate: None,
            ssh: None,
        })
        .await
        .unwrap()
}
#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn transaction_status_and_rollback() {
    let mut conn = connect().await;
    conn.execute(
        "CREATE TEMPORARY TABLE mysql_transaction_test (id INT)",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    let cursor = conn
        .execute(
            "INSERT INTO mysql_transaction_test VALUES (17)",
            QueryOptions {
                auto_commit: false,
                ..Default::default()
            },
        )
        .await
        .unwrap();
    assert_eq!(cursor.summary().transaction_active, Some(true));
    assert_eq!(cursor.summary().affected_rows, Some(1));
    conn.rollback().await.unwrap();
    let mut cursor = conn
        .execute(
            "SELECT count(*) FROM mysql_transaction_test",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(0)]]
    );
    assert_eq!(cursor.summary().transaction_active, Some(false));
    conn.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn query_error_does_not_destroy_session() {
    let mut conn = connect().await;
    assert_eq!(
        conn.execute("SELECT missing_column", QueryOptions::default())
            .await
            .err()
            .unwrap()
            .kind,
        ErrorKind::Query
    );
    let mut cursor = conn
        .execute("SELECT 23", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(23)]]
    );
}
#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn pages_and_exact_values() {
    let mut conn = connect().await;
    let mut cursor = conn.execute("SELECT CAST(18446744073709551615 AS UNSIGNED), CAST('1234567890123.4500' AS DECIMAL(20,4)), NULL, 'hello', CAST('2026-09-20' AS DATE), CAST('-27:04:05.123456' AS TIME(6)), X'00FF'", QueryOptions::default()).await.unwrap();
    let page = cursor.fetch_page(PageSize::default()).await.unwrap();
    assert_eq!(
        page.rows,
        vec![vec![
            Value::Decimal("18446744073709551615".into()),
            Value::Decimal("1234567890123.4500".into()),
            Value::Null,
            Value::Text("hello".into()),
            Value::Date("2026-09-20".into()),
            Value::Time("-27:04:05.123456".into()),
            Value::Binary(vec![0, 255])
        ]]
    );
    assert!(!page.has_more);
    let mut cursor = conn.execute("WITH RECURSIVE n AS (SELECT 1 AS i UNION ALL SELECT i+1 FROM n WHERE i<237) SELECT i FROM n ORDER BY i", QueryOptions::default()).await.unwrap();
    let first = cursor
        .fetch_page_bounded(PageSize::new(100).unwrap(), 32768)
        .await
        .unwrap();
    assert_eq!(first.rows.len(), 100);
    assert_eq!(first.rows[0], vec![Value::Integer(1)]);
    assert!(first.has_more);
    assert!(first.estimated_bytes() <= 32768);
    let second = cursor
        .fetch_page(PageSize::new(100).unwrap())
        .await
        .unwrap();
    assert_eq!(second.rows[0], vec![Value::Integer(101)]);
    assert!(second.has_more);
    let third = cursor
        .fetch_page(PageSize::new(100).unwrap())
        .await
        .unwrap();
    assert_eq!(third.rows.len(), 37);
    assert_eq!(third.rows[36], vec![Value::Integer(237)]);
    assert!(!third.has_more);
}
#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn cancellation_is_prompt_and_tokens_are_scoped() {
    let mut conn = connect().await;
    let stale = conn.cancellation_handle();
    conn.execute("SELECT 1", QueryOptions::default())
        .await
        .unwrap();
    stale.cancel().await.unwrap();
    conn.execute("SELECT 2", QueryOptions::default())
        .await
        .unwrap();
    let cancel = conn.cancellation_handle();
    let query = async {
        let mut slow = conn
            .execute("SELECT SLEEP(30)", QueryOptions::default())
            .await?;
        slow.fetch_page(PageSize::default()).await
    };
    let stop = async {
        tokio::time::sleep(std::time::Duration::from_millis(50)).await;
        cancel.cancel().await.unwrap();
    };
    let (result, ()) = tokio::time::timeout(std::time::Duration::from_secs(2), async {
        tokio::join!(query, stop)
    })
    .await
    .unwrap();
    assert_eq!(result.err().unwrap().kind, ErrorKind::Cancelled);
    let mut cursor = conn
        .execute("SELECT 3", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(3)]]
    );
}
#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn metadata_ddl_and_independent_object_cursor() {
    let mut conn = connect().await;
    conn.execute(
        "DROP TABLE IF EXISTS `mysql``browse`",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    conn.execute(
        "CREATE TABLE IF NOT EXISTS `mysql``browse` (`id` INT PRIMARY KEY, `la``bel` VARCHAR(30))",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    conn.execute("DELETE FROM `mysql``browse`", QueryOptions::default())
        .await
        .unwrap();
    conn.execute(
        "INSERT INTO `mysql``browse` VALUES (9,'nine')",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    let db = conn
        .load_metadata(None)
        .await
        .unwrap()
        .into_iter()
        .find(|o| o.name == "choscordb_test")
        .unwrap();
    let table = conn
        .load_metadata(Some(db.id))
        .await
        .unwrap()
        .into_iter()
        .find(|o| o.name == "mysql`browse")
        .unwrap();
    assert_eq!(table.kind, ObjectKind::Table);
    assert_eq!(table.qualified_name, "`choscordb_test`.`mysql``browse`");
    let fields = conn.load_metadata(Some(table.id.clone())).await.unwrap();
    assert_eq!(fields.len(), 2);
    assert_eq!(
        fields[1].qualified_name,
        "`choscordb_test`.`mysql``browse`.`la``bel`"
    );
    assert_eq!(fields[0].name, "id");
    assert_eq!(
        fields[0].qualified_name,
        "`choscordb_test`.`mysql``browse`.`id`"
    );
    assert!(
        conn.object_ddl(&table.id)
            .await
            .unwrap()
            .contains("PRIMARY KEY")
    );
    let mut sql = conn
        .execute("SELECT 73", QueryOptions::default())
        .await
        .unwrap();
    let mut object = conn.open_object(&table.id, 1024 * 1024).await.unwrap();
    assert_eq!(
        object.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(9), Value::Text("nine".into())]]
    );
    assert_eq!(object.summary().transaction_active, None);
    assert_eq!(object.columns().len(), 2);
    assert_eq!(
        sql.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(73)]]
    );
    conn.execute("DROP TABLE `mysql``browse`", QueryOptions::default())
        .await
        .unwrap();
}
#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn verified_tls_rejects_untrusted_server_as_tls_error() {
    let result = MysqlDriver
        .connect(ConnectionOptions::Mysql {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: None,
            proxy_secret: None,
            host: "127.0.0.1".into(),
            port: std::env::var("CHOSCORDB_MYSQL_PORT")
                .unwrap_or_else(|_| "33306".into())
                .parse()
                .unwrap(),
            database: "choscordb_test".into(),
            user: "root".into(),
            password: Some(Secret::new("choscordb-test-password")),
            ssh_secret: None,
            tls: TlsMode::VerifyFull,
            tls_identity: None,
            root_certificate: None,
            ssh: None,
        })
        .await;
    assert_eq!(result.err().unwrap().kind, ErrorKind::Tls);
}
#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn timeout_is_distinct_from_cancellation() {
    let mut conn = connect().await;
    let result = async {
        let mut slow = conn
            .execute(
                "SELECT SLEEP(30)",
                QueryOptions {
                    timeout: Some(std::time::Duration::from_millis(20)),
                    ..Default::default()
                },
            )
            .await?;
        slow.fetch_page(PageSize::default()).await
    }
    .await;
    assert_eq!(result.err().unwrap().kind, ErrorKind::Timeout);
    let mut next = conn
        .execute("SELECT 19", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        next.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(19)]]
    );
}
#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn bounded_page_can_retry_without_losing_the_row() {
    let mut conn = connect().await;
    let mut cursor = conn
        .execute(
            "SELECT REPEAT('a',1000) UNION ALL SELECT 'b'",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    assert_eq!(
        cursor
            .fetch_page_bounded(PageSize::default(), 64)
            .await
            .err()
            .unwrap()
            .kind,
        ErrorKind::ResourceLimit
    );
    let page = cursor
        .fetch_page_bounded(PageSize::default(), 16384)
        .await
        .unwrap();
    assert_eq!(
        page.rows,
        vec![
            vec![Value::Text("a".repeat(1000))],
            vec![Value::Text("b".into())]
        ]
    );
    assert!(!page.has_more);
    assert!(page.estimated_bytes() <= 16384);
}
#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn schema_budget_is_checked_before_writes() {
    let mut conn = connect().await;
    conn.execute(
        "DROP TABLE IF EXISTS mysql_budget_write",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    let failure = conn
        .execute_bounded(
            "CREATE TABLE mysql_budget_write (id INT)",
            QueryOptions::default(),
            0,
        )
        .await;
    assert_eq!(failure.err().unwrap().kind, ErrorKind::ResourceLimit);
    let mut observer = connect().await;
    let mut cursor=observer.execute("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA='choscordb_test' AND TABLE_NAME='mysql_budget_write'",QueryOptions::default()).await.unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(0)]]
    );
}
#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn slow_object_can_be_cancelled_without_closing_sql_session() {
    let mut conn = connect().await;
    conn.execute(
        "CREATE OR REPLACE VIEW mysql_slow_object AS SELECT SLEEP(30) AS sleepy",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    let db = conn
        .load_metadata(None)
        .await
        .unwrap()
        .into_iter()
        .find(|o| o.name == "choscordb_test")
        .unwrap();
    let view = conn
        .load_metadata(Some(db.id))
        .await
        .unwrap()
        .into_iter()
        .find(|o| o.name == "mysql_slow_object")
        .unwrap();
    let mut object = tokio::time::timeout(
        std::time::Duration::from_secs(2),
        conn.open_object(&view.id, 1024 * 1024),
    )
    .await
    .expect("opening object must not execute rows")
    .unwrap();
    let cancel = object.independent_cancellation_handle().unwrap();
    let stop = async {
        tokio::time::sleep(std::time::Duration::from_millis(50)).await;
        cancel.cancel().await.unwrap();
    };
    let (result, ()) = tokio::time::timeout(std::time::Duration::from_secs(2), async {
        tokio::join!(object.fetch_page(PageSize::default()), stop)
    })
    .await
    .unwrap();
    assert_eq!(result.err().unwrap().kind, ErrorKind::Cancelled);
    let mut cursor = conn
        .execute("SELECT 81", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(81)]]
    );
    conn.execute("DROP VIEW mysql_slow_object", QueryOptions::default())
        .await
        .unwrap();
}
#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn object_browsing_cannot_write_through_a_stored_function() {
    use mysql_async::prelude::Queryable;
    let mut fixture = mysql_async::Conn::new(
        mysql_async::OptsBuilder::default()
            .ip_or_hostname("127.0.0.1")
            .tcp_port(
                std::env::var("CHOSCORDB_MYSQL_PORT")
                    .unwrap_or_else(|_| "33306".into())
                    .parse()
                    .unwrap(),
            )
            .user(Some("root"))
            .pass(Some("choscordb-test-password"))
            .db_name(Some("choscordb_test"))
            .prefer_socket(false),
    )
    .await
    .unwrap();
    let mut conn = connect().await;
    for sql in [
        "DROP VIEW IF EXISTS mysql_write_view",
        "DROP FUNCTION IF EXISTS mysql_write_function",
        "DROP TABLE IF EXISTS mysql_write_target",
        "CREATE TABLE mysql_write_target (id INT)",
        "CREATE FUNCTION mysql_write_function() RETURNS INT DETERMINISTIC MODIFIES SQL DATA BEGIN INSERT INTO mysql_write_target VALUES (1); RETURN 1; END",
        "CREATE VIEW mysql_write_view AS SELECT mysql_write_function() AS value",
    ] {
        fixture.query_drop(sql).await.unwrap();
    }
    let db = conn
        .load_metadata(None)
        .await
        .unwrap()
        .into_iter()
        .find(|o| o.name == "choscordb_test")
        .unwrap();
    let view = conn
        .load_metadata(Some(db.id))
        .await
        .unwrap()
        .into_iter()
        .find(|o| o.name == "mysql_write_view")
        .unwrap();
    let mut cursor = conn.open_object(&view.id, 1024 * 1024).await.unwrap();
    let result = cursor.fetch_page(PageSize::default()).await;
    let mut count = conn
        .execute(
            "SELECT COUNT(*) FROM mysql_write_target",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    let rows = count.fetch_page(PageSize::default()).await.unwrap().rows;
    for sql in [
        "DROP VIEW mysql_write_view",
        "DROP FUNCTION mysql_write_function",
        "DROP TABLE mysql_write_target",
    ] {
        fixture.query_drop(sql).await.unwrap();
    }
    assert_eq!(
        result.expect_err("object source must reject writes").kind,
        ErrorKind::Query
    );
    assert_eq!(rows, vec![vec![Value::Integer(0)]]);
    assert_eq!(cursor.summary().transaction_active, None);
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn large_values_are_deferred_and_survive_cursor_close() {
    let mut conn = connect().await;
    let mut cursor = conn
        .execute(
            "SELECT REPEAT('x', 5000000), X'00FF'",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    let page = cursor
        .fetch_page_bounded(PageSize::default(), 32768)
        .await
        .unwrap();
    let Value::Deferred {
        handle,
        byte_length,
        ..
    } = page.rows[0][0]
    else {
        panic!("large text must be deferred")
    };
    assert_eq!(byte_length, 5000000);
    assert_eq!(page.rows[0][1], Value::Binary(vec![0, 255]));
    let reader = cursor.deferred_reader().unwrap();
    cursor.close().await.unwrap();
    assert_eq!(reader.read_chunk(handle, 4999997, 8).unwrap().bytes, b"xxx");
    assert_eq!(reader.read_chunk(handle, 0, 3).unwrap().bytes, b"xxx");
    assert!(reader.read_chunk(handle, 5000001, 8).is_err());
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn first_page_arrives_before_full_result_finishes() {
    let mut conn = connect().await;
    let mut cursor = tokio::time::timeout(std::time::Duration::from_secs(8), conn.execute(
        "WITH RECURSIVE n AS (SELECT 1 AS i UNION ALL SELECT i+1 FROM n WHERE i<900) SELECT i, REPEAT('x',4096), SLEEP(0.02) FROM n",
        QueryOptions::default(),
    )).await.expect("execute must not spool the full eighteen-second result").unwrap();
    let page = tokio::time::timeout(
        std::time::Duration::from_secs(10),
        cursor.fetch_page(PageSize::new(100).unwrap()),
    )
    .await
    .unwrap()
    .unwrap();
    assert_eq!(page.rows[0][0], Value::Integer(1));
    assert!(page.has_more);
    cursor.close().await.unwrap();
    conn.close().await.unwrap();
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn script_results_keep_schemas_and_follow_changed_sql_mode() {
    let mut conn = connect().await;
    let mut cursor = conn.execute("SET SESSION sql_mode='NO_BACKSLASH_ESCAPES'; SELECT 'a\\' AS first; SELECT 42 AS second", QueryOptions::default()).await.unwrap();
    assert!(cursor.columns().is_empty());
    assert!(cursor.next_result_set().await.unwrap());
    assert_eq!(cursor.columns()[0].name, "first");
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Text("a\\".into())]]
    );
    assert!(cursor.next_result_set().await.unwrap());
    assert_eq!(cursor.columns()[0].name, "second");
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(42)]]
    );
    assert!(!cursor.next_result_set().await.unwrap());
    assert_eq!(
        conn.sql_mode().await.unwrap().unwrap(),
        "NO_BACKSLASH_ESCAPES"
    );
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn stored_procedure_results_preserve_each_schema() {
    let mut conn = connect().await;
    conn.execute(
        "DROP PROCEDURE IF EXISTS mysql_many_results",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    use mysql_async::prelude::Queryable;
    let mut fixture = mysql_async::Conn::new(
        mysql_async::OptsBuilder::default()
            .ip_or_hostname("127.0.0.1")
            .tcp_port(
                std::env::var("CHOSCORDB_MYSQL_PORT")
                    .unwrap_or_else(|_| "33306".into())
                    .parse()
                    .unwrap(),
            )
            .db_name(Some("choscordb_test"))
            .user(Some("root"))
            .pass(Some("choscordb-test-password"))
            .prefer_socket(false),
    )
    .await
    .unwrap();
    fixture.query_drop("CREATE PROCEDURE mysql_many_results() BEGIN SELECT 12 AS first; SELECT 'two' AS second, 3 AS third; END").await.unwrap();
    let mut cursor = conn
        .execute("CALL mysql_many_results()", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(cursor.columns()[0].name, "first");
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(12)]]
    );
    assert!(cursor.summary().has_more_results);
    assert!(cursor.next_result_set().await.unwrap());
    assert_eq!(cursor.columns().len(), 2);
    assert_eq!(cursor.columns()[0].name, "second");
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Text("two".into()), Value::Integer(3)]]
    );
    while cursor.next_result_set().await.unwrap() {
        cursor.fetch_page(PageSize::default()).await.unwrap();
    }
    conn.execute("DROP PROCEDURE mysql_many_results", QueryOptions::default())
        .await
        .unwrap();
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn script_error_keeps_prior_results_and_stops_later_writes() {
    let mut conn = connect().await;
    let mut cursor = conn
        .execute(
            "SELECT 87 AS preserved; SELECT missing_script_column; SET @should_not_run=99",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(87)]]
    );
    assert_eq!(
        cursor.next_result_set().await.unwrap_err().kind,
        ErrorKind::Query
    );
    let mut check = conn
        .execute("SELECT @should_not_run", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        check.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Null]]
    );
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn metadata_pages_browse_more_than_ten_thousand_objects() {
    use mysql_async::prelude::Queryable;
    let mut fixture = mysql_async::Conn::new(
        mysql_async::OptsBuilder::default()
            .ip_or_hostname("127.0.0.1")
            .tcp_port(
                std::env::var("CHOSCORDB_MYSQL_PORT")
                    .unwrap_or_else(|_| "33306".into())
                    .parse()
                    .unwrap(),
            )
            .user(Some("root"))
            .pass(Some("choscordb-test-password"))
            .prefer_socket(false),
    )
    .await
    .unwrap();
    fixture
        .query_drop("DROP DATABASE IF EXISTS mysql_metadata_pages")
        .await
        .unwrap();
    fixture
        .query_drop("CREATE DATABASE mysql_metadata_pages")
        .await
        .unwrap();
    for i in 0..10001 {
        fixture
            .query_drop(format!(
                "CREATE VIEW mysql_metadata_pages.v{i:05} AS SELECT 1 AS n"
            ))
            .await
            .unwrap();
    }
    let mut conn = connect().await;
    let parent = ObjectId("[\"mysql_metadata_pages\"]".into());
    let first = conn
        .load_metadata_page(Some(parent.clone()), 0, 10000)
        .await;
    let last = conn.load_metadata_page(Some(parent), 10000, 10000).await;
    fixture
        .query_drop("DROP DATABASE mysql_metadata_pages")
        .await
        .unwrap();
    let first = first.unwrap();
    let last = last.unwrap();
    assert_eq!(first.objects.len(), 10000);
    assert_eq!(first.objects[0].name, "v00000");
    assert_eq!(first.next_offset, Some(10000));
    assert_eq!(last.objects.len(), 1);
    assert_eq!(last.objects[0].name, "v10000");
    assert_eq!(last.next_offset, None);
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn leading_set_is_ready_and_cancelling_script_stops_later_writes() {
    let mut conn = connect().await;
    let cancel = conn.cancellation_handle();
    let mut cursor = tokio::time::timeout(
        std::time::Duration::from_secs(2),
        conn.execute(
            "SET @script_started=1; SELECT SLEEP(30); SET @cancelled_script_write=99",
            QueryOptions::default(),
        ),
    )
    .await
    .expect("first non-row result must not await later statements")
    .unwrap();
    assert!(cursor.columns().is_empty());
    assert!(cursor.summary().has_more_results);
    let first = tokio::time::timeout(
        std::time::Duration::from_secs(2),
        cursor.fetch_page(PageSize::default()),
    )
    .await
    .expect("first empty page must not await later statements")
    .unwrap();
    assert!(first.rows.is_empty());
    assert!(!first.has_more);
    cancel.cancel().await.unwrap();
    // Advancing may obtain the interrupted set or its cancellation error.
    let result = match cursor.next_result_set().await {
        Ok(true) => cursor.fetch_page(PageSize::default()).await.map(|_| ()),
        Ok(false) => panic!("interrupted result must report cancellation"),
        Err(error) => Err(error),
    };
    assert_eq!(result.unwrap_err().kind, ErrorKind::Cancelled);
    let mut next = conn
        .execute(
            "SELECT @script_started, @cancelled_script_write",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    assert_eq!(
        next.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(1), Value::Null]]
    );
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn wide_text_row_defers_aggregate_and_keeps_bounded_page() {
    let mut conn = connect().await;
    let sql = format!(
        "SELECT {}",
        (0..100)
            .map(|i| format!("REPEAT('x', 10000) AS c{i}"))
            .collect::<Vec<_>>()
            .join(",")
    );
    let mut cursor = conn.execute(&sql, QueryOptions::default()).await.unwrap();
    let page = cursor
        .fetch_page_bounded(PageSize::default(), 1024 * 1024)
        .await
        .unwrap();
    assert_eq!(page.rows[0].len(), 100);
    assert!(page.estimated_bytes() <= 1024 * 1024);
    assert!(page.rows[0].iter().any(|v| matches!(
        v,
        Value::Deferred {
            byte_length: 10000,
            ..
        }
    )));
    assert_eq!(page.rows[0][0], Value::Text("x".repeat(10000)));
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn native_transport_admission_releases_on_close_and_drop() {
    let mut connections = Vec::new();
    for _ in 0..8 {
        connections.push(connect().await);
    }
    let options = || ConnectionOptions::Mysql {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host: "127.0.0.1".into(),
        port: std::env::var("CHOSCORDB_MYSQL_PORT")
            .unwrap_or_else(|_| "33306".into())
            .parse()
            .unwrap(),
        database: "choscordb_test".into(),
        user: "root".into(),
        password: Some(Secret::new("choscordb-test-password")),
        ssh_secret: None,
        tls: TlsMode::Disable,
        tls_identity: None,
        root_certificate: None,
        ssh: None,
    };
    assert_eq!(
        MysqlDriver.connect(options()).await.err().unwrap().kind,
        ErrorKind::ResourceLimit
    );
    connections[0].close().await.unwrap();
    let replacement = MysqlDriver.connect(options()).await.unwrap();
    assert_eq!(
        MysqlDriver.connect(options()).await.err().unwrap().kind,
        ErrorKind::ResourceLimit
    );
    drop(replacement);
    let mut replacement = MysqlDriver.connect(options()).await.unwrap();
    replacement.close().await.unwrap();
    for connection in &mut connections {
        connection.close().await.unwrap();
    }
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn ambiguous_text_fallback_is_rejected_before_side_effects() {
    let mut conn = connect().await;
    conn.execute(
        "DROP PROCEDURE IF EXISTS mysql_ambiguous_routine",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    let result = conn
        .execute(
            "CREATE PROCEDURE mysql_ambiguous_routine() BEGIN SELECT 1; END",
            QueryOptions::default(),
        )
        .await;
    let failure = result
        .err()
        .expect("compound fallback must not bypass statement cancellation boundaries");
    assert_eq!(failure.kind, ErrorKind::Unsupported);
    let mut observer = connect().await;
    let mut cursor = observer.execute("SELECT COUNT(*) FROM information_schema.ROUTINES WHERE ROUTINE_SCHEMA='choscordb_test' AND ROUTINE_NAME='mysql_ambiguous_routine'", QueryOptions::default()).await.unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(0)]]
    );
}
