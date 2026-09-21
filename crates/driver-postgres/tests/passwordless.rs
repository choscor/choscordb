use choscordb_driver_api::{ConnectionOptions, DatabaseDriver, TlsMode};
use choscordb_driver_postgres::PostgresDriver;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

#[tokio::test]
async fn direct_connection_accepts_server_trust_authentication_without_a_password() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut socket, _) = listener.accept().await.unwrap();
        let length = socket.read_u32().await.unwrap();
        let mut startup = vec![0; length as usize - 4];
        socket.read_exact(&mut startup).await.unwrap();
        socket
            .write_all(b"R\0\0\0\x08\0\0\0\0Z\0\0\0\x05I")
            .await
            .unwrap();
        let mut termination = [0; 1];
        let read = socket.read(&mut termination).await.unwrap();
        assert!(read == 0 || termination == *b"X");
    });
    let mut connection = PostgresDriver
        .connect(ConnectionOptions::Postgres {
            host: "127.0.0.1".into(),
            port,
            database: "postgres".into(),
            user: "local".into(),
            password: None,
            ssh_secret: None,
            tls: TlsMode::Disable,
            root_certificate: None,
            ssh: None,
        })
        .await
        .unwrap();
    connection.close().await.unwrap();
    server.await.unwrap();
}
