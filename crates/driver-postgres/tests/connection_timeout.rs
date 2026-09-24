use choscordb_driver_api::{ConnectionOptions, DatabaseDriver, ErrorKind, TlsMode};
use choscordb_driver_postgres::PostgresDriver;
use std::time::Duration;
use tokio::io::AsyncReadExt;

async fn assert_stalled_connection_times_out(tls: TlsMode) {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let expect_tls = tls == TlsMode::VerifyFull;
    let server = tokio::spawn(async move {
        let (mut socket, _) = listener.accept().await.unwrap();
        let length = socket.read_u32().await.unwrap();
        let mut request = vec![0; length as usize - 4];
        socket.read_exact(&mut request).await.unwrap();
        if expect_tls {
            assert_eq!(request, [4, 210, 22, 47]); // PostgreSQL SSLRequest
        } else {
            assert_eq!(&request[..4], &[0, 3, 0, 0]); // StartupMessage
        }
        // Accept TCP but never answer the TLS/startup request. The client must
        // bound the protocol handshake and release its socket on timeout.
        let mut byte = [0];
        assert_eq!(socket.read(&mut byte).await.unwrap(), 0);
    });
    let result = tokio::time::timeout(
        Duration::from_secs(20),
        PostgresDriver.connect(ConnectionOptions::Postgres {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: None,
            proxy_secret: None,
            host: "127.0.0.1".into(),
            port,
            database: "postgres".into(),
            user: "local".into(),
            password: None,
            ssh_secret: None,
            tls,
            tls_identity: None,
            root_certificate: None,
            ssh: None,
        }),
    )
    .await
    .expect("direct connection must time out after its 15-second deadline");
    let error = match result {
        Err(error) => error,
        Ok(_) => panic!("silent server cannot establish a connection"),
    };
    assert_eq!(error.kind, ErrorKind::Timeout);
    tokio::time::timeout(Duration::from_secs(2), server)
        .await
        .expect("timed-out connection must close its socket")
        .unwrap();
}

#[tokio::test]
async fn direct_connection_bounds_startup_and_authentication() {
    assert_stalled_connection_times_out(TlsMode::Disable).await;
}

#[tokio::test]
async fn direct_connection_bounds_tls_negotiation() {
    assert_stalled_connection_times_out(TlsMode::VerifyFull).await;
}
