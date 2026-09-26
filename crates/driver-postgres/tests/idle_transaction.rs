use choscordb_driver_api::*;
use choscordb_driver_postgres::PostgresDriver;
fn options() -> ConnectionOptions {
    let config: tokio_postgres::Config = std::env::var("CHOSCORDB_TEST_POSTGRES")
        .unwrap()
        .parse()
        .unwrap();
    ConnectionOptions::Postgres {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host: "localhost".into(),
        port: config.get_ports()[0],
        database: config.get_dbname().unwrap().into(),
        user: config.get_user().unwrap().into(),
        password: Some(Secret::new(
            std::str::from_utf8(config.get_password().unwrap()).unwrap(),
        )),
        ssh: None,
        ssh_secret: None,
        tls: TlsMode::Disable,
        root_certificate: None,
        tls_identity: None,
    }
}
async fn run(connection: &mut dyn Connection, sql: &str) {
    let mut cursor = connection
        .execute(
            sql,
            QueryOptions {
                auto_commit: false,
                ..Default::default()
            },
        )
        .await
        .unwrap();
    while cursor
        .fetch_page(PageSize::default())
        .await
        .unwrap()
        .has_more
    {}
    cursor.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable PostgreSQL fixture"]
async fn raw_transaction_controls_keep_native_state_and_commit_visibility_truthful() {
    let mut connection = PostgresDriver.connect(options()).await.unwrap();
    run(
        &mut *connection,
        "CREATE TEMP TABLE raw_controls(value INTEGER)",
    )
    .await;
    run(&mut *connection, "INSERT INTO raw_controls VALUES(1)").await;
    run(&mut *connection, "COMMIT").await;
    assert_eq!(connection.transaction_state().await.unwrap(), Some(false));
    assert!(
        !connection
            .idle_transaction_state()
            .await
            .unwrap()
            .unwrap()
            .active
    );
    connection
        .execute("BEGIN READ WRITE", QueryOptions::default())
        .await
        .unwrap()
        .close()
        .await
        .unwrap();
    connection
        .execute(
            "INSERT INTO raw_controls VALUES(2)",
            QueryOptions::default(),
        )
        .await
        .unwrap()
        .close()
        .await
        .unwrap();
    assert!(
        connection
            .idle_transaction_state()
            .await
            .unwrap()
            .unwrap()
            .manual
    );
    run(&mut *connection, "ROLLBACK AND CHAIN").await;
    let fresh = connection.idle_transaction_state().await.unwrap().unwrap();
    assert!(fresh.active && fresh.manual && !fresh.write_pending);
    let mut cursor = connection
        .execute(
            "SELECT value FROM raw_controls ORDER BY value",
            QueryOptions {
                auto_commit: false,
                ..Default::default()
            },
        )
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(1)]]
    );
    cursor.close().await.unwrap();
    run(&mut *connection, "ROLLBACK").await;
    assert_eq!(connection.transaction_state().await.unwrap(), Some(false));
    connection.close().await.unwrap();
}
