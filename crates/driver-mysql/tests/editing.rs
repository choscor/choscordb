//! Run with CHOSCORDB_MYSQL_TEST=1 against the disposable MySQL test server.
use choscordb_driver_api::*;
use choscordb_driver_mysql::MysqlDriver;
async fn connect() -> Box<dyn Connection> {
    MysqlDriver
        .connect(ConnectionOptions::Mysql {
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
            root_certificate: None,
            ssh: None,
        })
        .await
        .unwrap()
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn primary_key_metadata_and_query_projection() {
    let mut conn = connect().await;
    for sql in [
        "DROP TABLE IF EXISTS mysql_edit_metadata",
        "CREATE TABLE mysql_edit_metadata (id INT PRIMARY KEY, label VARCHAR(80), doubled INT GENERATED ALWAYS AS (id * 2) STORED)",
    ] {
        conn.execute(sql, QueryOptions::default())
            .await
            .unwrap()
            .close()
            .await
            .unwrap();
    }
    let object = ObjectId(r#"["choscordb_test","mysql_edit_metadata"]"#.into());
    let target = conn.inspect_edit_target(&object).await.unwrap();
    assert_eq!(
        target.qualified_name,
        "`choscordb_test`.`mysql_edit_metadata`"
    );
    assert_eq!(target.parameter_style, "?");
    assert_eq!(target.key_columns, ["id"]);
    assert!(target.columns[2].generated);
    let query = conn
        .inspect_edit_query(
            "SELECT `id`, `label` AS `name` FROM `mysql_edit_metadata` WHERE id = 1",
            vec!["id".into(), "name".into()],
        )
        .await
        .unwrap();
    assert!(query.reason.is_empty(), "{}", query.reason);
    assert_eq!(query.source_columns, ["id", "label"]);
    for (sql, columns) in [
        (
            "SELECT label FROM mysql_edit_metadata",
            vec!["label".into()],
        ),
        (
            "SELECT a.id FROM mysql_edit_metadata a JOIN mysql_edit_metadata b ON a.id=b.id",
            vec!["id".into()],
        ),
        (
            "SELECT id, id FROM mysql_edit_metadata",
            vec!["id".into(), "id".into()],
        ),
    ] {
        assert!(
            !conn
                .inspect_edit_query(sql, columns)
                .await
                .unwrap()
                .reason
                .is_empty()
        );
    }
    conn.execute("DROP TABLE mysql_edit_metadata", QueryOptions::default())
        .await
        .unwrap()
        .close()
        .await
        .unwrap();
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn reviewed_edits_bind_values_and_roll_back_conflicts() {
    let mut conn = connect().await;
    for sql in [
        "DROP TABLE IF EXISTS mysql_edit_batch",
        "CREATE TABLE mysql_edit_batch (id INT PRIMARY KEY, label TEXT, amount DECIMAL(20,4), bytes BLOB)",
        "INSERT INTO mysql_edit_batch VALUES (1,'original',1,NULL)",
    ] {
        conn.execute(sql, QueryOptions::default())
            .await
            .unwrap()
            .close()
            .await
            .unwrap();
    }
    let update = EditStatement {
        sql: "UPDATE mysql_edit_batch SET label=?, amount=?, bytes=? WHERE id=? AND label <=> ?"
            .into(),
        params: vec![
            Value::Text("'; DROP TABLE mysql_edit_batch; -- \\".into()),
            Value::Decimal("1234567890123.4500".into()),
            Value::Binary(vec![0, 255]),
            Value::Integer(1),
            Value::Text("original".into()),
        ],
        expected_rows: Some(1),
    };
    let summary = conn
        .apply_edit_batch(EditBatch {
            statements: vec![update.clone()],
        })
        .await
        .unwrap();
    assert_eq!(summary.affected_rows, [1]);
    let mut cursor = conn
        .execute(
            "SELECT label,amount,bytes FROM mysql_edit_batch WHERE id=1",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![
            update.params[0].clone(),
            update.params[1].clone(),
            update.params[2].clone()
        ]]
    );
    cursor.close().await.unwrap();
    let unchanged = EditStatement {
        sql: "UPDATE mysql_edit_batch SET amount=? WHERE id=?".into(),
        params: vec![
            Value::Decimal("1234567890123.4500".into()),
            Value::Integer(1),
        ],
        expected_rows: Some(1),
    };
    assert_eq!(
        conn.apply_edit_batch(EditBatch {
            statements: vec![unchanged]
        })
        .await
        .unwrap()
        .affected_rows,
        [1]
    );
    let insert = EditStatement {
        sql: "INSERT INTO mysql_edit_batch (id,label) VALUES (?,?)".into(),
        params: vec![Value::Integer(2), Value::Text("must roll back".into())],
        expected_rows: None,
    };
    assert!(
        conn.apply_edit_batch(EditBatch {
            statements: vec![insert, update]
        })
        .await
        .is_err()
    );
    let mut cursor = conn
        .execute(
            "SELECT COUNT(*) FROM mysql_edit_batch",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(1)]]
    );
    cursor.close().await.unwrap();
    conn.execute(
        "SELECT 1",
        QueryOptions {
            auto_commit: false,
            ..Default::default()
        },
    )
    .await
    .unwrap()
    .close()
    .await
    .unwrap();
    assert!(
        conn.apply_edit_batch(EditBatch { statements: vec![] })
            .await
            .is_err()
    );
    conn.rollback().await.unwrap();
    conn.execute("DROP TABLE mysql_edit_batch", QueryOptions::default())
        .await
        .unwrap()
        .close()
        .await
        .unwrap();
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn nontransactional_targets_and_views_are_read_only() {
    let mut conn = connect().await;
    for sql in [
        "DROP VIEW IF EXISTS mysql_edit_view",
        "DROP TABLE IF EXISTS mysql_edit_nontransactional",
        "CREATE TABLE mysql_edit_nontransactional (id INT PRIMARY KEY) ENGINE=MyISAM",
        "CREATE VIEW mysql_edit_view AS SELECT * FROM mysql_edit_nontransactional",
    ] {
        conn.execute(sql, QueryOptions::default())
            .await
            .unwrap()
            .close()
            .await
            .unwrap();
    }
    for table in ["mysql_edit_nontransactional", "mysql_edit_view"] {
        let target = conn
            .inspect_edit_target(&ObjectId(format!(r#"["choscordb_test","{table}"]"#)))
            .await
            .unwrap();
        assert!(target.columns.is_empty());
        assert!(!target.reason.is_empty());
    }
    for sql in [
        "DROP VIEW mysql_edit_view",
        "DROP TABLE mysql_edit_nontransactional",
    ] {
        conn.execute(sql, QueryOptions::default())
            .await
            .unwrap()
            .close()
            .await
            .unwrap();
    }
}
