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
async fn object_graph_preserves_direct_composite_parallel_self_and_cross_schema_keys() {
    let mut conn = connect().await;
    for sql in [
        "DROP DATABASE IF EXISTS erd_graph_aux",
        "DROP VIEW IF EXISTS erd_graph_view",
        "DROP TABLE IF EXISTS erd_graph_solo",
        "DROP TABLE IF EXISTS erd_graph_far",
        "DROP TABLE IF EXISTS erd_graph_child",
        "DROP TABLE IF EXISTS erd_graph_center",
        "DROP TABLE IF EXISTS erd_graph_leaf",
        "DROP TABLE IF EXISTS erd_graph_peer",
        "CREATE TABLE erd_graph_peer (id INT, code INT, PRIMARY KEY (id, code))",
        "CREATE TABLE erd_graph_leaf (id INT PRIMARY KEY)",
        "CREATE TABLE erd_graph_center (id INT PRIMARY KEY, parent_id INT, peer_id INT, peer_code INT, CONSTRAINT erd_graph_self FOREIGN KEY (parent_id) REFERENCES erd_graph_center(id), CONSTRAINT erd_graph_composite FOREIGN KEY (peer_id, peer_code) REFERENCES erd_graph_peer(id, code))",
        "CREATE TABLE erd_graph_child (id INT PRIMARY KEY, center_id INT, other_center_id INT, leaf_id INT, CONSTRAINT erd_graph_inbound_one FOREIGN KEY (center_id) REFERENCES erd_graph_center(id), CONSTRAINT erd_graph_inbound_two FOREIGN KEY (other_center_id) REFERENCES erd_graph_center(id), CONSTRAINT erd_graph_second_hop FOREIGN KEY (leaf_id) REFERENCES erd_graph_leaf(id))",
        "CREATE TABLE erd_graph_far (id INT PRIMARY KEY, child_id INT, FOREIGN KEY (child_id) REFERENCES erd_graph_child(id))",
        "CREATE TABLE erd_graph_solo (id BIGINT PRIMARY KEY, note VARCHAR(24))",
        "CREATE VIEW erd_graph_view AS SELECT id FROM erd_graph_solo",
        "CREATE DATABASE erd_graph_aux",
        "CREATE TABLE erd_graph_aux.outside_table (id INT PRIMARY KEY, center_id INT, CONSTRAINT erd_graph_cross FOREIGN KEY (center_id) REFERENCES choscordb_test.erd_graph_center(id))",
    ] {
        conn.execute(sql, QueryOptions::default()).await.unwrap();
    }
    let graph = conn
        .load_object_graph(&ObjectId(r#"["choscordb_test","erd_graph_center"]"#.into()))
        .await
        .unwrap();
    assert_eq!(graph.availability, MetadataAvailability::Available);
    assert_eq!(graph.tables.len(), 4);
    assert_eq!(graph.edges.len(), 5);
    assert!(
        !graph
            .tables
            .iter()
            .any(|table| table.qualified_name.contains("erd_graph_leaf"))
    );
    assert!(
        !graph
            .tables
            .iter()
            .any(|table| table.qualified_name.contains("erd_graph_far"))
    );
    let center = graph
        .tables
        .iter()
        .find(|table| table.qualified_name == "`choscordb_test`.`erd_graph_center`")
        .unwrap();
    assert_eq!(
        center
            .columns
            .iter()
            .map(|column| column.name.as_str())
            .collect::<Vec<_>>(),
        vec!["id", "parent_id", "peer_id", "peer_code"]
    );
    assert_eq!(center.columns[0].database_type, "int");
    assert!(center.columns[0].primary_key);
    assert!(center.columns[1].foreign_key);
    assert!(center.columns[2].foreign_key);
    assert!(center.columns[3].foreign_key);
    let composite = graph
        .edges
        .iter()
        .find(|edge| edge.id.contains("erd_graph_composite"))
        .unwrap();
    assert_eq!(composite.source_columns, ["peer_id", "peer_code"]);
    assert_eq!(composite.target_columns, ["id", "code"]);
    assert_eq!(
        composite.target_id,
        ObjectId(r#"["choscordb_test","erd_graph_peer"]"#.into())
    );
    let self_edge = graph
        .edges
        .iter()
        .find(|edge| edge.id.contains("erd_graph_self"))
        .unwrap();
    assert_eq!(self_edge.source_id, self_edge.target_id);
    let inbound = graph
        .edges
        .iter()
        .filter(|edge| edge.id.contains("erd_graph_inbound_"))
        .collect::<Vec<_>>();
    assert_eq!(inbound.len(), 2);
    assert!(inbound.iter().all(|edge| edge.target_id == center.id));
    let child = graph
        .tables
        .iter()
        .find(|table| table.qualified_name == "`choscordb_test`.`erd_graph_child`")
        .unwrap();
    assert!(
        child
            .columns
            .iter()
            .find(|column| column.name == "leaf_id")
            .unwrap()
            .foreign_key
    );
    let cross = graph
        .edges
        .iter()
        .find(|edge| edge.id.contains("erd_graph_cross"))
        .unwrap();
    assert_eq!(
        cross.source_id,
        ObjectId(r#"["erd_graph_aux","outside_table"]"#.into())
    );
    let solo = conn
        .load_object_graph(&ObjectId(r#"["choscordb_test","erd_graph_solo"]"#.into()))
        .await
        .unwrap();
    assert_eq!(solo.availability, MetadataAvailability::Available);
    assert_eq!(solo.tables.len(), 1);
    assert!(solo.edges.is_empty());
    assert_eq!(solo.tables[0].columns[0].database_type, "bigint");
    let view = conn
        .load_object_graph(&ObjectId(r#"["choscordb_test","erd_graph_view"]"#.into()))
        .await
        .unwrap();
    assert_eq!(view.availability, MetadataAvailability::Unsupported);
    for sql in [
        "DROP DATABASE erd_graph_aux",
        "DROP VIEW erd_graph_view",
        "DROP TABLE erd_graph_solo",
        "DROP TABLE erd_graph_far",
        "DROP TABLE erd_graph_child",
        "DROP TABLE erd_graph_center",
        "DROP TABLE erd_graph_leaf",
        "DROP TABLE erd_graph_peer",
    ] {
        conn.execute(sql, QueryOptions::default()).await.unwrap();
    }
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn object_graph_identifies_related_tables_beyond_its_limit() {
    let mut conn = connect().await;
    conn.execute(
        "DROP DATABASE IF EXISTS erd_graph_limit",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    conn.execute("CREATE DATABASE erd_graph_limit", QueryOptions::default())
        .await
        .unwrap();
    conn.execute(
        "CREATE TABLE erd_graph_limit.center_table (id INT PRIMARY KEY)",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    for index in 0..65 {
        conn.execute(
            &format!("CREATE TABLE erd_graph_limit.child_{index:03} (id INT PRIMARY KEY, center_id INT, CONSTRAINT fk_child_{index:03} FOREIGN KEY (center_id) REFERENCES center_table(id))"),
            QueryOptions::default(),
        ).await.unwrap();
    }
    let graph = conn
        .load_object_graph(&ObjectId(r#"["erd_graph_limit","center_table"]"#.into()))
        .await
        .unwrap();
    assert_eq!(graph.availability, MetadataAvailability::Unavailable);
    assert!(graph.reason.contains("related-table limit"));
    assert_eq!(graph.tables.len(), 64);
    assert_eq!(graph.edges.len(), 65);
    assert!(
        graph
            .warnings
            .iter()
            .any(|warning| warning.contains("child_064") && warning.contains("fk_child_064"))
    );
    conn.execute("DROP DATABASE erd_graph_limit", QueryOptions::default())
        .await
        .unwrap();
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn object_graph_keeps_complete_constraints_at_fk_column_limit() {
    let mut conn = connect().await;
    conn.execute(
        "DROP DATABASE IF EXISTS erd_graph_fk_limit",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    conn.execute(
        "CREATE DATABASE erd_graph_fk_limit",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    conn.execute("CREATE TABLE erd_graph_fk_limit.center_table (a INT, b INT, c INT, d INT, e INT, PRIMARY KEY (a,b,c,d,e))", QueryOptions::default()).await.unwrap();
    let mut parts = vec!["id INT PRIMARY KEY".to_owned()];
    for index in 0..52 {
        let names = (0..5)
            .map(|part| format!("c{index:03}_{part}"))
            .collect::<Vec<_>>();
        parts.extend(names.iter().map(|name| format!("`{name}` INT")));
        parts.push(format!(
            "CONSTRAINT fk_{index:03} FOREIGN KEY ({}) REFERENCES center_table(a,b,c,d,e)",
            names
                .iter()
                .map(|name| format!("`{name}`"))
                .collect::<Vec<_>>()
                .join(",")
        ));
    }
    conn.execute(
        &format!(
            "CREATE TABLE erd_graph_fk_limit.child_table ({})",
            parts.join(",")
        ),
        QueryOptions::default(),
    )
    .await
    .unwrap();
    let graph = conn
        .load_object_graph(&ObjectId(r#"["erd_graph_fk_limit","center_table"]"#.into()))
        .await
        .unwrap();
    assert_eq!(graph.availability, MetadataAvailability::Unavailable);
    assert!(graph.reason.contains("relationship limit"));
    assert_eq!(graph.tables.len(), 2);
    assert_eq!(graph.edges.len(), 51);
    assert!(
        graph
            .edges
            .iter()
            .all(|edge| edge.source_columns.len() == 5 && edge.target_columns.len() == 5)
    );
    assert!(
        graph
            .warnings
            .iter()
            .any(|warning| warning.contains("fk_051"))
    );
    conn.execute("DROP DATABASE erd_graph_fk_limit", QueryOptions::default())
        .await
        .unwrap();
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn object_graph_reports_aggregate_column_limit_without_losing_edges() {
    let mut conn = connect().await;
    conn.execute(
        "DROP DATABASE IF EXISTS erd_graph_columns",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    conn.execute("CREATE DATABASE erd_graph_columns", QueryOptions::default())
        .await
        .unwrap();
    conn.execute(
        "CREATE TABLE erd_graph_columns.center_table (id INT PRIMARY KEY)",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    for index in 0..6 {
        let mut columns = vec!["id INT PRIMARY KEY".to_owned(), "center_id INT".to_owned()];
        columns.extend((0..230).map(|column| format!("payload_{column:03} INT")));
        columns.push(format!(
            "CONSTRAINT fk_wide_{index:02} FOREIGN KEY (center_id) REFERENCES center_table(id)"
        ));
        conn.execute(
            &format!(
                "CREATE TABLE erd_graph_columns.wide_{index:02} ({})",
                columns.join(",")
            ),
            QueryOptions::default(),
        )
        .await
        .unwrap();
    }
    let graph = conn
        .load_object_graph(&ObjectId(r#"["erd_graph_columns","center_table"]"#.into()))
        .await
        .unwrap();
    assert_eq!(graph.availability, MetadataAvailability::Unavailable);
    assert_eq!(graph.edges.len(), 6);
    assert!(
        graph
            .tables
            .iter()
            .map(|table| table.columns.len())
            .sum::<usize>()
            <= 1024
    );
    assert!(
        graph
            .warnings
            .iter()
            .any(|warning| warning.contains("wide_"))
    );
    conn.execute("DROP DATABASE erd_graph_columns", QueryOptions::default())
        .await
        .unwrap();
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn object_graph_reports_aggregate_text_limit_without_losing_edges() {
    let mut conn = connect().await;
    conn.execute(
        "DROP DATABASE IF EXISTS erd_graph_text",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    conn.execute("CREATE DATABASE erd_graph_text", QueryOptions::default())
        .await
        .unwrap();
    conn.execute(
        "CREATE TABLE erd_graph_text.center_table (id INT PRIMARY KEY)",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    for index in 0..3 {
        let mut columns = vec!["id INT PRIMARY KEY".to_owned(), "center_id INT".to_owned()];
        columns.extend((0..250).map(|column| format!("`p{column:03}_{}` INT", "x".repeat(54))));
        columns.push(format!(
            "CONSTRAINT fk_text_{index:02} FOREIGN KEY (center_id) REFERENCES center_table(id)"
        ));
        conn.execute(
            &format!(
                "CREATE TABLE erd_graph_text.wide_{index:02} ({})",
                columns.join(",")
            ),
            QueryOptions::default(),
        )
        .await
        .unwrap();
    }
    let graph = conn
        .load_object_graph(&ObjectId(r#"["erd_graph_text","center_table"]"#.into()))
        .await
        .unwrap();
    assert_eq!(graph.availability, MetadataAvailability::Unavailable);
    assert_eq!(graph.edges.len(), 3);
    let text_bytes: usize = graph
        .tables
        .iter()
        .flat_map(|table| &table.columns)
        .map(|column| column.name.len() + column.database_type.len())
        .sum();
    assert!(text_bytes <= 32 * 1024);
    assert!(
        graph
            .warnings
            .iter()
            .any(|warning| warning.contains("wide_"))
    );
    conn.execute("DROP DATABASE erd_graph_text", QueryOptions::default())
        .await
        .unwrap();
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn object_graph_reports_oversized_type_without_truncating_it() {
    let mut conn = connect().await;
    conn.execute(
        "DROP DATABASE IF EXISTS erd_graph_type",
        QueryOptions::default(),
    )
    .await
    .unwrap();
    conn.execute("CREATE DATABASE erd_graph_type", QueryOptions::default())
        .await
        .unwrap();
    let variants = (0..300)
        .map(|index| format!("'v{index:03}_{}'", "x".repeat(110)))
        .collect::<Vec<_>>()
        .join(",");
    conn.execute(&format!("CREATE TABLE erd_graph_type.center_table (id INT PRIMARY KEY, large_kind ENUM({variants}))"), QueryOptions::default()).await.unwrap();
    let admitted_variants = (0..100)
        .map(|index| format!("'v{index:03}_{}'", "x".repeat(110)))
        .collect::<Vec<_>>()
        .join(",");
    conn.execute(&format!("CREATE TABLE erd_graph_type.admitted_table (id INT PRIMARY KEY, kind ENUM({admitted_variants}))"), QueryOptions::default()).await.unwrap();
    let graph = conn
        .load_object_graph(&ObjectId(r#"["erd_graph_type","center_table"]"#.into()))
        .await
        .unwrap();
    assert_eq!(graph.availability, MetadataAvailability::Unavailable);
    assert!(
        graph
            .warnings
            .iter()
            .any(|warning| warning.contains("center_table"))
    );
    assert_eq!(graph.tables.len(), 1);
    assert!(graph.tables[0].columns.is_empty());
    let admitted = conn
        .load_object_graph(&ObjectId(r#"["erd_graph_type","admitted_table"]"#.into()))
        .await
        .unwrap();
    assert_eq!(admitted.availability, MetadataAvailability::Available);
    assert_eq!(
        admitted.tables[0].columns[1].database_type,
        format!("enum({admitted_variants})")
    );
    conn.execute("DROP DATABASE erd_graph_type", QueryOptions::default())
        .await
        .unwrap();
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
    // Send a substantial prefix immediately, then block the final row. This
    // proves streaming without depending on accumulated per-row sleep timing
    // or the server flushing each small group of delayed rows.
    let mut cursor = tokio::time::timeout(std::time::Duration::from_secs(8), conn.execute(
        "WITH RECURSIVE n AS (SELECT 1 AS i UNION ALL SELECT i+1 FROM n WHERE i<900) SELECT i, REPEAT('x',4096), IF(i=900,SLEEP(30),0) FROM n",
        QueryOptions::default(),
    )).await.expect("execute must not wait for the blocked result tail").unwrap();
    let page = tokio::time::timeout(
        std::time::Duration::from_secs(10),
        cursor.fetch_page(PageSize::new(100).unwrap()),
    )
    .await
    .expect("first page must arrive while the result tail is blocked")
    .unwrap();
    assert_eq!(page.rows.len(), 100);
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
