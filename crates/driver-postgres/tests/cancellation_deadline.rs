use choscordb_driver_api::*;
use choscordb_driver_postgres::PostgresDriver;
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

#[tokio::test]
async fn direct_cancel_bounds_stalled_tls_negotiation_and_closes_socket() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let (query_started, query_ready) = tokio::sync::oneshot::channel();
    let server = tokio::spawn(async move {
        let (mut connection, _) = listener.accept().await.unwrap();
        let mut ssl = [0; 8];
        connection.read_exact(&mut ssl).await.unwrap();
        assert_eq!(ssl, [0, 0, 0, 8, 4, 210, 22, 47]);
        connection.write_all(b"N").await.unwrap();
        let length = connection.read_u32().await.unwrap();
        let mut startup = vec![0; length as usize - 4];
        connection.read_exact(&mut startup).await.unwrap();
        connection
            .write_all(&[
                b'R', 0, 0, 0, 8, 0, 0, 0, 0, b'K', 0, 0, 0, 12, 0, 0, 0, 1, 0, 0, 0, 2, b'Z', 0,
                0, 0, 5, b'I',
            ])
            .await
            .unwrap();
        assert_eq!(connection.read_u8().await.unwrap(), b'Q');
        let length = connection.read_u32().await.unwrap();
        let mut begin = vec![0; length as usize - 4];
        connection.read_exact(&mut begin).await.unwrap();
        assert_eq!(begin, b"START TRANSACTION\0");
        connection
            .write_all(&[
                b'C', 0, 0, 0, 10, b'B', b'E', b'G', b'I', b'N', 0, b'Z', 0, 0, 0, 5, b'T',
            ])
            .await
            .unwrap();
        assert_eq!(connection.read_u8().await.unwrap(), b'Q');
        let length = connection.read_u32().await.unwrap();
        let mut setup = vec![0; length as usize - 4];
        connection.read_exact(&mut setup).await.unwrap();
        assert_eq!(setup, b"SET LOCAL statement_timeout = 0\0");
        query_started.send(()).unwrap();
        let (mut cancel, _) = listener.accept().await.unwrap();
        cancel.read_exact(&mut ssl).await.unwrap();
        assert_eq!(ssl, [0, 0, 0, 8, 4, 210, 22, 47]);
        let mut byte = [0];
        assert_eq!(cancel.read(&mut byte).await.unwrap(), 0);
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
        let _ = connection
            .execute("SELECT 1", QueryOptions::default())
            .await;
    });
    tokio::time::timeout(Duration::from_secs(3), query_ready)
        .await
        .unwrap()
        .unwrap();
    let result = tokio::time::timeout(Duration::from_secs(18), cancel.cancel()).await;
    query.abort();
    let error = result
        .expect("cancellation must honor the connection deadline")
        .unwrap_err();
    assert_eq!(error.kind, ErrorKind::Timeout);
    tokio::time::timeout(Duration::from_secs(2), server)
        .await
        .unwrap()
        .unwrap();
}
