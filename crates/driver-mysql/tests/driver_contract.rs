use choscordb_driver_api::*;
use choscordb_driver_mysql::MysqlDriver;
fn settings() -> ConnectionOptions {
    ConnectionOptions::Mysql {
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
    }
}
#[path = "../../driver-api/tests/support/driver_contract.rs"]
mod contract;

#[tokio::test]
#[ignore = "requires disposable mysql fixture"]
async fn bounded_results() {
    contract::bounded_results(&MysqlDriver, settings).await;
}

#[tokio::test]
#[ignore = "requires disposable mysql fixture"]
async fn close_rolls_back() {
    contract::close_rolls_back(&MysqlDriver, settings).await;
}

#[tokio::test]
#[ignore = "requires disposable mysql fixture"]
async fn cancellation_generation() {
    contract::cancellation_generation(&MysqlDriver, settings).await;
}

#[tokio::test]
#[ignore = "requires disposable mysql fixture"]
async fn schema_preflight_preserves_session() {
    contract::schema_preflight_preserves_session(&MysqlDriver, settings).await;
}
