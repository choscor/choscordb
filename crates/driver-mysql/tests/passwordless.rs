//! Run against a disposable MySQL server configured for empty-password TCP login.
use choscordb_driver_api::{ConnectionOptions, DatabaseDriver, TlsMode};
use choscordb_driver_mysql::MysqlDriver;

#[tokio::test]
#[ignore = "requires disposable passwordless MySQL server"]
async fn direct_connection_accepts_a_user_without_a_password() {
    let port = std::env::var("CHOSCORDB_TEST_MYSQL_PASSWORDLESS_PORT")
        .expect("fixture port")
        .parse()
        .unwrap();
    let mut connection = MysqlDriver
        .connect(ConnectionOptions::Mysql {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: None,
            proxy_secret: None,
            host: "127.0.0.1".into(),
            port,
            database: "choscordb_test".into(),
            user: "root".into(),
            password: None,
            ssh_secret: None,
            tls: TlsMode::Disable,
            tls_identity: None,
            root_certificate: None,
            ssh: None,
        })
        .await
        .unwrap();
    connection.close().await.unwrap();
}
