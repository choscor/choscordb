use choscordb_driver_api::*;
use choscordb_driver_postgres::PostgresDriver;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
#[tokio::test]
async fn postgres_connects_through_proxy_without_resolving_database_host_locally() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut socket, _) = listener.accept().await.unwrap();
        let mut greeting = [0; 3];
        socket.read_exact(&mut greeting).await.unwrap();
        assert_eq!(greeting, [5, 1, 0]);
        socket.write_all(&[5, 0]).await.unwrap();
        let mut header = [0; 5];
        socket.read_exact(&mut header).await.unwrap();
        assert_eq!(&header[..4], [5, 1, 0, 3]);
        let mut destination = vec![0; usize::from(header[4]) + 2];
        socket.read_exact(&mut destination).await.unwrap();
        assert_eq!(
            &destination[..destination.len() - 2],
            b"database.example.invalid"
        );
        socket
            .write_all(&[5, 0, 0, 1, 127, 0, 0, 1, 0, 0])
            .await
            .unwrap();
        let size = socket.read_u32().await.unwrap();
        let mut startup = vec![0; size as usize - 4];
        socket.read_exact(&mut startup).await.unwrap();
        assert_eq!(&startup[..4], [0, 3, 0, 0]);
        socket
            .write_all(b"R\0\0\0\x08\0\0\0\0Z\0\0\0\x05I")
            .await
            .unwrap();
        let mut terminate = [0; 5];
        let _ = socket.read_exact(&mut terminate).await;
    });
    let result = PostgresDriver
        .connect(ConnectionOptions::Postgres {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: Some(SocksProxy {
                protocol: SocksProtocol::Socks5,
                host: "127.0.0.1".into(),
                port,
                username: None,
            }),
            proxy_secret: None,
            host: "database.example.invalid".into(),
            port: 5432,
            database: "db".into(),
            user: "alice".into(),
            password: None,
            ssh: None,
            ssh_secret: None,
            tls: TlsMode::Disable,
            root_certificate: None,
            tls_identity: None,
        })
        .await;
    result
        .expect("proxy must carry database protocol")
        .close()
        .await
        .unwrap();
    server.await.unwrap();
}

#[path = "../../driver-api/tests/support/socks_proxy.rs"]
mod socks_proxy;
#[tokio::test]
#[ignore = "requires disposable PostgreSQL TLS fixture"]
async fn verified_tls_and_cancellation_keep_the_original_database_hostname_through_proxy() {
    use std::{sync::atomic::Ordering, time::Duration};
    let config: tokio_postgres::Config = std::env::var("CHOSCORDB_TEST_POSTGRES")
        .unwrap()
        .parse()
        .unwrap();
    let port = config.get_ports()[0];
    let (proxy, server, count) = socks_proxy::start(port).await;
    let mut connection = PostgresDriver
        .connect(ConnectionOptions::Postgres {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: Some(proxy),
            proxy_secret: Some(Secret::new("secret")),
            host: "localhost".into(),
            port,
            database: config.get_dbname().unwrap().into(),
            user: config.get_user().unwrap().into(),
            password: Some(Secret::new(
                std::str::from_utf8(config.get_password().unwrap()).unwrap(),
            )),
            ssh: None,
            ssh_secret: None,
            tls: TlsMode::VerifyFull,
            root_certificate: Some(
                std::env::var_os("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE")
                    .unwrap()
                    .into(),
            ),
            tls_identity: None,
        })
        .await
        .unwrap();
    let mut cursor = connection
        .execute("SELECT 731", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(731)]]
    );
    cursor.close().await.unwrap();
    let cancel = connection.cancellation_handle();
    let (result, ()) = tokio::time::timeout(Duration::from_secs(8), async {
        tokio::join!(
            async {
                let mut cursor = connection
                    .execute("SELECT pg_sleep(30)", QueryOptions::default())
                    .await?;
                cursor.fetch_page(PageSize::default()).await
            },
            async {
                tokio::time::sleep(Duration::from_millis(100)).await;
                cancel.cancel().await.unwrap();
            }
        )
    })
    .await
    .unwrap();
    assert_eq!(result.err().unwrap().kind, ErrorKind::Cancelled);
    connection.close().await.unwrap();
    drop(connection);
    assert!(
        count.load(Ordering::SeqCst) >= 2,
        "cancellation must use the proxy too"
    );
    server.abort();
}

#[tokio::test]
#[ignore = "requires disposable PostgreSQL TLS fixture"]
async fn proxy_cannot_replace_the_database_certificate_hostname() {
    use std::sync::atomic::Ordering;
    let config: tokio_postgres::Config = std::env::var("CHOSCORDB_TEST_POSTGRES")
        .unwrap()
        .parse()
        .unwrap();
    let port = config.get_ports()[0];
    let (proxy, server, count) = socks_proxy::start_for_host(port, "wrong-cert.invalid").await;
    let result = PostgresDriver
        .connect(ConnectionOptions::Postgres {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: Some(proxy),
            proxy_secret: Some(Secret::new("secret")),
            host: "wrong-cert.invalid".into(),
            port,
            database: config.get_dbname().unwrap().into(),
            user: config.get_user().unwrap().into(),
            password: Some(Secret::new(
                std::str::from_utf8(config.get_password().unwrap()).unwrap(),
            )),
            ssh: None,
            ssh_secret: None,
            tls: TlsMode::VerifyFull,
            root_certificate: Some(
                std::env::var_os("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE")
                    .unwrap()
                    .into(),
            ),
            tls_identity: None,
        })
        .await;
    assert_eq!(
        result
            .err()
            .expect("wrong database certificate name must fail")
            .kind,
        ErrorKind::Tls
    );
    assert!(
        count.load(Ordering::SeqCst) >= 1,
        "must reach TLS through proxy"
    );
    server.abort();
}
