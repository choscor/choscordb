//! Requires the disposable MySQL fixture described in docs/testing/mysql.md.
use choscordb_driver_api::*;
use choscordb_driver_mysql::MysqlDriver;

#[tokio::test]
#[ignore = "requires disposable MySQL server"]
async fn server_connection_leaves_database_unselected_and_preserves_explicit_database() {
    for (database, expected) in [
        ("", Value::Null),
        ("choscordb_test", Value::Text("choscordb_test".into())),
    ] {
        let mut connection = MysqlDriver
            .connect(ConnectionOptions::Mysql {
                ssh_jump_secrets: Default::default(),
                ssh_private_key: None,
                ssh_jump_private_keys: Default::default(),
                proxy: None,
                proxy_secret: None,
                host: "localhost".into(),
                port: std::env::var("CHOSCORDB_MYSQL_PORT")
                    .expect("fixture port")
                    .parse()
                    .unwrap(),
                database: database.into(),
                user: "root".into(),
                password: Some(Secret::new("choscordb-test-password")),
                ssh_secret: None,
                tls: TlsMode::Disable,
                tls_identity: None,
                root_certificate: None,
                ssh: None,
            })
            .await
            .unwrap();
        let mut cursor = connection
            .execute("SELECT DATABASE()", QueryOptions::default())
            .await
            .unwrap();
        assert_eq!(
            cursor.fetch_page(PageSize::default()).await.unwrap().rows,
            vec![vec![expected]]
        );
        cursor.close().await.unwrap();
        connection.close().await.unwrap();
    }
}
