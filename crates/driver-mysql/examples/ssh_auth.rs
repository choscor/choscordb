use choscordb_driver_api::{
    ConnectionOptions, DatabaseDriver, PageSize, QueryOptions, Secret, SshAuthentication,
    SshTunnel, TlsMode, Value, run_ssh_askpass_if_requested,
};
use choscordb_driver_mysql::MysqlDriver;

fn value(name: &str) -> String {
    std::env::var(name).unwrap_or_else(|_| panic!("missing {name}"))
}

async fn verify(authentication: SshAuthentication, identity: Option<String>, secret: String) {
    let mut connection = MysqlDriver
        .connect(ConnectionOptions::Mysql {
            host: "mysql-ssh-target".into(),
            port: 3306,
            database: "choscordb_test".into(),
            user: "root".into(),
            password: Some(Secret::new("choscordb-test-password")),
            ssh_secret: Some(Secret::new(secret)),
            tls: TlsMode::Disable,
            root_certificate: None,
            ssh: Some(SshTunnel {
                host: "127.0.0.1".into(),
                port: value("CHOSCORDB_SSH_PORT").parse().unwrap(),
                user: "root".into(),
                authentication,
                identity_file: identity,
            }),
        })
        .await
        .unwrap();
    let mut cursor = connection
        .execute("SELECT 811", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(811)]]
    );
    cursor.close().await.unwrap();
    connection.close().await.unwrap();
}

fn main() {
    if let Some(code) = run_ssh_askpass_if_requested() {
        std::process::exit(code);
    }
    let runtime = tokio::runtime::Runtime::new().unwrap();
    runtime.block_on(async {
        verify(
            SshAuthentication::Password,
            None,
            value("CHOSCORDB_SSH_PASSWORD"),
        )
        .await;
        verify(
            SshAuthentication::PublicKey,
            Some(value("CHOSCORDB_SSH_ENCRYPTED_IDENTITY")),
            value("CHOSCORDB_SSH_KEY_PASSPHRASE"),
        )
        .await;
    });
}
