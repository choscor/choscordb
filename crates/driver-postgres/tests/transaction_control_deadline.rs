use choscordb_driver_api::*;
use choscordb_driver_postgres::PostgresDriver;
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
#[tokio::test]
async fn raw_begin_deadline_closes_a_stalled_native_control() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut stream, _) = listener.accept().await.unwrap();
        let length = stream.read_u32().await.unwrap();
        let mut startup = vec![0; length as usize - 4];
        stream.read_exact(&mut startup).await.unwrap();
        stream
            .write_all(&[
                b'R', 0, 0, 0, 8, 0, 0, 0, 0, b'K', 0, 0, 0, 12, 0, 0, 0, 1, 0, 0, 0, 2, b'Z', 0,
                0, 0, 5, b'I',
            ])
            .await
            .unwrap();
        assert_eq!(stream.read_u8().await.unwrap(), b'Q');
        let length = stream.read_u32().await.unwrap();
        let mut sql = vec![0; length as usize - 4];
        stream.read_exact(&mut sql).await.unwrap();
        assert_eq!(sql, b"START TRANSACTION\0");
        let mut rest = Vec::new();
        tokio::time::timeout(Duration::from_secs(3), stream.read_to_end(&mut rest))
            .await
            .expect("control deadline must dispose transport")
            .unwrap();
    });
    let mut connection = PostgresDriver
        .connect(ConnectionOptions::Postgres {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: None,
            proxy_secret: None,
            host: "127.0.0.1".into(),
            port,
            database: "db".into(),
            user: "user".into(),
            password: None,
            ssh_secret: None,
            tls: TlsMode::Disable,
            root_certificate: None,
            tls_identity: None,
            ssh: None,
        })
        .await
        .unwrap();

    let error = tokio::time::timeout(
        Duration::from_secs(3),
        connection.execute(
            "BEGIN",
            QueryOptions {
                timeout: Some(Duration::from_millis(100)),
                ..Default::default()
            },
        ),
    )
    .await
    .expect("public control must honor its deadline")
    .err()
    .unwrap();
    assert_eq!(error.kind, ErrorKind::Timeout);
    tokio::time::timeout(Duration::from_secs(3), server)
        .await
        .unwrap()
        .unwrap();
    assert_eq!(
        connection.transaction_state().await.unwrap_err().kind,
        ErrorKind::Disconnected
    );
}

#[tokio::test]
async fn raw_control_deadline_includes_concurrent_cancel_bookkeeping() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let (query_started, query_ready) = tokio::sync::oneshot::channel();
    let (cancel_started, cancel_ready) = tokio::sync::oneshot::channel();
    let server = tokio::spawn(async move {
        let (mut stream, _) = listener.accept().await.unwrap();
        let mut ssl = [0; 8];
        stream.read_exact(&mut ssl).await.unwrap();
        stream.write_all(b"N").await.unwrap();
        let length = stream.read_u32().await.unwrap();
        let mut startup = vec![0; length as usize - 4];
        stream.read_exact(&mut startup).await.unwrap();
        stream
            .write_all(&[
                b'R', 0, 0, 0, 8, 0, 0, 0, 0, b'K', 0, 0, 0, 12, 0, 0, 0, 1, 0, 0, 0, 2, b'Z', 0,
                0, 0, 5, b'I',
            ])
            .await
            .unwrap();
        assert_eq!(stream.read_u8().await.unwrap(), b'Q');
        let length = stream.read_u32().await.unwrap();
        let mut sql = vec![0; length as usize - 4];
        stream.read_exact(&mut sql).await.unwrap();
        assert_eq!(sql, b"START TRANSACTION\0");
        query_started.send(()).unwrap();
        let (mut cancelling, _) = listener.accept().await.unwrap();
        cancelling.read_exact(&mut ssl).await.unwrap();
        cancel_started.send(()).unwrap();
        let mut rest = Vec::new();
        tokio::time::timeout(Duration::from_secs(3), stream.read_to_end(&mut rest))
            .await
            .expect("control deadline must dispose transport")
            .unwrap();
    });
    let mut connection = PostgresDriver
        .connect(ConnectionOptions::Postgres {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: None,
            proxy_secret: None,
            host: "127.0.0.1".into(),
            port,
            database: "db".into(),
            user: "user".into(),
            password: None,
            ssh_secret: None,
            tls: TlsMode::Prefer,
            root_certificate: None,
            tls_identity: None,
            ssh: None,
        })
        .await
        .unwrap();

    let cancel = connection.cancellation_handle();
    let query = tokio::spawn(async move {
        connection
            .execute(
                "BEGIN",
                QueryOptions {
                    timeout: Some(Duration::from_millis(500)),
                    ..Default::default()
                },
            )
            .await
            .err()
            .unwrap()
    });
    query_ready.await.unwrap();
    let cancellation = tokio::spawn(async move { cancel.cancel().await });
    cancel_ready.await.unwrap();
    let result = tokio::time::timeout(Duration::from_secs(3), query).await;
    cancellation.abort();
    assert_eq!(
        result
            .expect("cancel bookkeeping must not outlive query deadline")
            .unwrap()
            .kind,
        ErrorKind::Timeout
    );
    tokio::time::timeout(Duration::from_secs(3), server)
        .await
        .unwrap()
        .unwrap();
}
