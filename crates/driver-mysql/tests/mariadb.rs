//! MariaDB-only behavior. Run with CHOSCORDB_MARIADB_PORT set to the disposable
//! MariaDB test server; see support/server.rs.
use choscordb_driver_api::*;
#[path = "support/server.rs"]
mod server;

async fn connect() -> Box<dyn Connection> {
    server::driver()
        .connect(server::options(TlsMode::Disable))
        .await
        .unwrap()
}

async fn run(conn: &mut dyn Connection, sql: &str) {
    conn.execute(sql, QueryOptions::default()).await.unwrap();
}

async fn rows(conn: &mut dyn Connection, sql: &str) -> Vec<Row> {
    let mut cursor = conn.execute(sql, QueryOptions::default()).await.unwrap();
    let rows = cursor.fetch_page(PageSize::default()).await.unwrap().rows;
    cursor.close().await.unwrap();
    rows
}

fn as_user(user: &str, password: &str) -> ConnectionOptions {
    let ConnectionOptions::Mariadb {
        proxy,
        proxy_secret,
        host,
        port,
        database,
        ssh_secret,
        ssh_jump_secrets,
        ssh_private_key,
        ssh_jump_private_keys,
        tls,
        root_certificate,
        tls_identity,
        ssh,
        ..
    } = server::options(TlsMode::Disable)
    else {
        panic!("the MariaDB tests need CHOSCORDB_MARIADB_PORT")
    };
    ConnectionOptions::Mariadb {
        proxy,
        proxy_secret,
        host,
        port,
        database,
        user: user.into(),
        password: Some(Secret::new(password)),
        ssh_secret,
        ssh_jump_secrets,
        ssh_private_key,
        ssh_jump_private_keys,
        tls,
        root_certificate,
        tls_identity,
        ssh,
    }
}

#[tokio::test]
#[ignore = "requires disposable MariaDB server"]
async fn ed25519_accounts_authenticate() {
    let mut admin = connect().await;
    if rows(
        admin.as_mut(),
        "SELECT COUNT(*) FROM information_schema.PLUGINS WHERE PLUGIN_NAME='ed25519'",
    )
    .await
        == vec![vec![Value::Integer(0)]]
    {
        run(admin.as_mut(), "INSTALL SONAME 'auth_ed25519'").await;
    }
    run(
        admin.as_mut(),
        "DROP USER IF EXISTS 'choscordb_ed25519'@'%'",
    )
    .await;
    run(
        admin.as_mut(),
        "CREATE USER 'choscordb_ed25519'@'%' IDENTIFIED VIA ed25519 USING PASSWORD('ed-secret')",
    )
    .await;
    run(
        admin.as_mut(),
        "GRANT SELECT ON choscordb_test.* TO 'choscordb_ed25519'@'%'",
    )
    .await;
    let mut user = server::driver()
        .connect(as_user("choscordb_ed25519", "ed-secret"))
        .await
        .unwrap();
    assert_eq!(
        rows(user.as_mut(), "SELECT CURRENT_USER()").await,
        vec![vec![Value::Text("choscordb_ed25519@%".into())]]
    );
    let wrong = server::driver()
        .connect(as_user("choscordb_ed25519", "wrong"))
        .await
        .err()
        .unwrap();
    assert_eq!(wrong.kind, ErrorKind::Authentication);
}

#[tokio::test]
#[ignore = "requires disposable MariaDB server"]
async fn parsec_accounts_fail_with_an_authentication_error_naming_parsec() {
    let mut admin = connect().await;
    let Value::Text(version) = rows(admin.as_mut(), "SELECT VERSION()").await[0][0].clone() else {
        panic!("VERSION() is text")
    };
    let mut parts = version
        .split(['.', '-'])
        .map(|part| part.parse::<u32>().unwrap_or(0));
    if (parts.next().unwrap(), parts.next().unwrap()) < (11, 6) {
        eprintln!("skipped: MariaDB {version} predates parsec authentication");
        return;
    }
    if rows(
        admin.as_mut(),
        "SELECT COUNT(*) FROM information_schema.PLUGINS WHERE PLUGIN_NAME='parsec'",
    )
    .await
        == vec![vec![Value::Integer(0)]]
    {
        run(admin.as_mut(), "INSTALL SONAME 'auth_parsec'").await;
    }
    run(admin.as_mut(), "DROP USER IF EXISTS 'choscordb_parsec'@'%'").await;
    run(
        admin.as_mut(),
        "CREATE USER 'choscordb_parsec'@'%' IDENTIFIED VIA parsec USING PASSWORD('parsec-secret')",
    )
    .await;
    let failure = server::driver()
        .connect(as_user("choscordb_parsec", "parsec-secret"))
        .await
        .err()
        .unwrap();
    assert_eq!(failure.kind, ErrorKind::Authentication);
    assert!(failure.message.contains("parsec"), "{}", failure.message);
}

fn property<'a>(object: &'a SchemaObject, name: &str) -> Option<&'a str> {
    object
        .properties
        .iter()
        .find(|property| property.name == name)
        .map(|property| property.value.as_str())
}

#[tokio::test]
#[ignore = "requires disposable MariaDB server"]
async fn sequences_and_system_versioned_tables_have_their_own_catalog_kinds() {
    let mut conn = connect().await;
    for sql in [
        "DROP TABLE IF EXISTS catalog_versioned, catalog_plain",
        "DROP SEQUENCE IF EXISTS catalog_sequence",
        "DROP VIEW IF EXISTS catalog_view",
        "CREATE SEQUENCE catalog_sequence",
        "CREATE TABLE catalog_versioned (id INT PRIMARY KEY) WITH SYSTEM VERSIONING",
        "CREATE TABLE catalog_plain (id INT PRIMARY KEY)",
        "CREATE VIEW catalog_view AS SELECT id FROM catalog_plain",
    ] {
        run(conn.as_mut(), sql).await;
    }
    let objects = conn
        .load_metadata(Some(ObjectId(r#"["choscordb_test"]"#.into())))
        .await
        .unwrap();
    let find = |name: &str| objects.iter().find(|object| object.name == name).unwrap();
    let sequence = find("catalog_sequence");
    assert_eq!(sequence.kind, ObjectKind::Sequence);
    assert!(!sequence.has_children);
    let versioned = find("catalog_versioned");
    assert_eq!(versioned.kind, ObjectKind::Table);
    assert!(versioned.has_children);
    assert_eq!(
        property(versioned, "Relation subtype"),
        Some("system_versioned")
    );
    let plain = find("catalog_plain");
    assert_eq!(plain.kind, ObjectKind::Table);
    assert_eq!(property(plain, "Relation subtype"), None);
    assert_eq!(find("catalog_view").kind, ObjectKind::View);
    let system = conn
        .load_metadata(Some(ObjectId(r#"["information_schema"]"#.into())))
        .await
        .unwrap();
    assert_eq!(
        system
            .iter()
            .find(|object| object.name == "TABLES")
            .unwrap()
            .kind,
        ObjectKind::View
    );
}

#[tokio::test]
#[ignore = "requires disposable MariaDB server"]
async fn uuid_and_inet_values_are_text() {
    let mut conn = connect().await;
    run(conn.as_mut(), "DROP TABLE IF EXISTS typed_values").await;
    run(
        conn.as_mut(),
        "CREATE TABLE typed_values (u UUID, v4 INET4, v6 INET6)",
    )
    .await;
    run(
        conn.as_mut(),
        "INSERT INTO typed_values VALUES ('123e4567-e89b-12d3-a456-426614174000', '192.0.2.1', '2001:db8::1')",
    )
    .await;
    assert_eq!(
        rows(conn.as_mut(), "SELECT u, v4, v6 FROM typed_values").await,
        vec![vec![
            Value::Text("123e4567-e89b-12d3-a456-426614174000".into()),
            Value::Text("192.0.2.1".into()),
            Value::Text("2001:db8::1".into()),
        ]]
    );
}

#[tokio::test]
#[ignore = "requires disposable MariaDB server"]
async fn system_versioned_tables_have_er_diagrams() {
    let mut conn = connect().await;
    for sql in [
        "DROP TABLE IF EXISTS graph_versioned_child, graph_versioned",
        "CREATE TABLE graph_versioned (id INT PRIMARY KEY) WITH SYSTEM VERSIONING",
        "CREATE TABLE graph_versioned_child (id INT PRIMARY KEY, parent_id INT, FOREIGN KEY (parent_id) REFERENCES graph_versioned(id))",
    ] {
        run(conn.as_mut(), sql).await;
    }
    let graph = conn
        .load_object_graph(&ObjectId(r#"["choscordb_test","graph_versioned"]"#.into()))
        .await
        .unwrap();
    assert_eq!(graph.availability, MetadataAvailability::Available);
    assert_eq!(graph.edges.len(), 1);
}

#[tokio::test]
#[ignore = "requires disposable MariaDB server"]
async fn system_versioned_tables_edit_current_rows_without_period_columns() {
    let mut conn = connect().await;
    for sql in [
        "DROP TABLE IF EXISTS edit_versioned_implicit, edit_versioned_explicit",
        "CREATE TABLE edit_versioned_implicit (id INT PRIMARY KEY, label TEXT) WITH SYSTEM VERSIONING",
        "CREATE TABLE edit_versioned_explicit (id INT PRIMARY KEY, label TEXT, \
         valid_from TIMESTAMP(6) GENERATED ALWAYS AS ROW START INVISIBLE, \
         valid_to TIMESTAMP(6) GENERATED ALWAYS AS ROW END INVISIBLE, \
         PERIOD FOR SYSTEM_TIME(valid_from, valid_to)) WITH SYSTEM VERSIONING",
        "INSERT INTO edit_versioned_explicit (id, label) VALUES (1, 'original')",
    ] {
        run(conn.as_mut(), sql).await;
    }
    for table in ["edit_versioned_implicit", "edit_versioned_explicit"] {
        let target = conn
            .inspect_edit_target(&ObjectId(format!(r#"["choscordb_test","{table}"]"#)))
            .await
            .unwrap();
        assert_eq!(target.reason, "", "{table}");
        assert_eq!(
            target
                .columns
                .iter()
                .map(|column| column.name.as_str())
                .collect::<Vec<_>>(),
            ["id", "label"],
            "{table}"
        );
        assert_eq!(target.key_columns, ["id"], "{table}");
    }
    let summary = conn
        .apply_edit_batch(EditBatch {
            statements: vec![EditStatement {
                sql: "UPDATE edit_versioned_explicit SET label=? WHERE id=?".into(),
                params: vec![Value::Text("changed".into()), Value::Integer(1)],
                expected_rows: Some(1),
            }],
        })
        .await
        .unwrap();
    assert_eq!(summary.affected_rows, [1]);
    assert_eq!(
        rows(
            conn.as_mut(),
            "SELECT label FROM edit_versioned_explicit FOR SYSTEM_TIME ALL ORDER BY valid_from"
        )
        .await,
        vec![
            vec![Value::Text("original".into())],
            vec![Value::Text("changed".into())]
        ]
    );
}

#[tokio::test]
#[ignore = "requires disposable MariaDB server"]
async fn history_reads_name_explicit_period_columns_once() {
    let mut conn = connect().await;
    for sql in [
        "DROP TABLE IF EXISTS history_hidden, history_shown",
        "CREATE TABLE history_hidden (id INT PRIMARY KEY, \
         valid_from TIMESTAMP(6) GENERATED ALWAYS AS ROW START INVISIBLE, \
         valid_to TIMESTAMP(6) GENERATED ALWAYS AS ROW END INVISIBLE, \
         PERIOD FOR SYSTEM_TIME(valid_from, valid_to)) WITH SYSTEM VERSIONING",
        "CREATE TABLE history_shown (id INT PRIMARY KEY, \
         valid_from TIMESTAMP(6) GENERATED ALWAYS AS ROW START, \
         valid_to TIMESTAMP(6) GENERATED ALWAYS AS ROW END, \
         PERIOD FOR SYSTEM_TIME(valid_from, valid_to)) WITH SYSTEM VERSIONING",
    ] {
        run(conn.as_mut(), sql).await;
    }
    for table in ["history_hidden", "history_shown"] {
        let mut cursor = conn
            .open_object(
                &ObjectId(format!(r#"["choscordb_test","{table}"]"#)),
                &ObjectDataTime::All,
                1024 * 1024,
            )
            .await
            .unwrap();
        assert_eq!(
            cursor
                .columns()
                .iter()
                .map(|column| column.name.as_str())
                .collect::<Vec<_>>(),
            ["id", "valid_from", "valid_to"],
            "{table}"
        );
        cursor.close().await.unwrap();
    }
}

#[tokio::test]
#[ignore = "requires disposable MariaDB server"]
async fn scripts_run_mariadb_executable_comment_statements() {
    let mut conn = connect().await;
    run(conn.as_mut(), "DROP TABLE IF EXISTS executable_comment").await;
    let mut cursor = conn
        .execute(
            "SELECT 1; /*M!100000 CREATE TABLE executable_comment (id INT) */; SELECT 2;",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    while cursor.next_result_set().await.unwrap() {}
    cursor.close().await.unwrap();
    assert_eq!(
        rows(
            conn.as_mut(),
            "SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA = 'choscordb_test' AND TABLE_NAME = 'executable_comment'"
        )
        .await,
        vec![vec![Value::Integer(1)]]
    );
}

#[tokio::test]
#[ignore = "requires disposable MariaDB server"]
async fn prefer_and_require_tls_accept_the_default_self_signed_certificate() {
    let mut admin = connect().await;
    if rows(admin.as_mut(), "SELECT @@have_ssl").await != vec![vec![Value::Text("YES".into())]] {
        eprintln!("skipped: this MariaDB server has TLS disabled");
        return;
    }
    for tls in [TlsMode::Prefer, TlsMode::Require] {
        let mut conn = server::driver()
            .connect(server::options(tls.clone()))
            .await
            .unwrap();
        let cipher = rows(conn.as_mut(), "SHOW SESSION STATUS LIKE 'Ssl_cipher'").await;
        assert!(
            matches!(&cipher[0][1], Value::Text(name) if !name.is_empty()),
            "{tls:?}"
        );
    }
}
