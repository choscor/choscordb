use choscordb_driver_api::*;
use choscordb_driver_postgres::PostgresDriver;
use std::time::Duration;

#[tokio::test]
async fn bracketed_ipv6_reaches_the_tcp_server() {
    let listener = tokio::net::TcpListener::bind((std::net::Ipv6Addr::LOCALHOST, 0))
        .await
        .unwrap();
    let port = listener.local_addr().unwrap().port();
    let attempt = tokio::spawn(async move {
        PostgresDriver
            .connect(ConnectionOptions::Postgres {
                ssh_jump_secrets: Default::default(),
                ssh_private_key: None,
                ssh_jump_private_keys: Default::default(),
                proxy: None,
                proxy_secret: None,
                host: "[::1]".into(),
                port,
                database: "app".into(),
                user: "reader".into(),
                password: None,
                ssh_secret: None,
                tls: TlsMode::Disable,
                tls_identity: None,
                root_certificate: None,
                ssh: None,
            })
            .await
    });
    let socket = tokio::time::timeout(Duration::from_secs(2), listener.accept()).await;
    attempt.abort();
    assert!(
        socket.is_ok(),
        "bracketed IPv6 did not reach the loopback listener"
    );
}

#[tokio::test]
async fn invalid_endpoint_and_nul_credentials_fail_before_network_io() {
    for (host, database, user) in [
        ("user@localhost", "app", "reader"),
        ("localhost:1234", "app", "reader"),
        ("localhost", "app\0name", "reader"),
        ("localhost", "app", "reader\0name"),
    ] {
        let result = PostgresDriver
            .connect(ConnectionOptions::Postgres {
                ssh_jump_secrets: Default::default(),
                ssh_private_key: None,
                ssh_jump_private_keys: Default::default(),
                proxy: None,
                proxy_secret: None,
                host: host.into(),
                port: 5432,
                database: database.into(),
                user: user.into(),
                password: None,
                ssh_secret: None,
                tls: TlsMode::Disable,
                tls_identity: None,
                root_certificate: None,
                ssh: None,
            })
            .await;
        assert!(
            matches!(
                result,
                Err(DriverError {
                    kind: ErrorKind::InvalidInput,
                    ..
                })
            ),
            "invalid field was not rejected"
        );
    }
}
