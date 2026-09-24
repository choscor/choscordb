use choscordb_driver_api::*;
use choscordb_driver_mysql::MysqlDriver;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
#[tokio::test]
async fn mysql_proxy_authentication_errors_are_not_hidden_as_database_failures() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut socket, _) = listener.accept().await.unwrap();
        let mut greeting = [0; 3];
        socket.read_exact(&mut greeting).await.unwrap();
        assert_eq!(greeting, [5, 1, 2]);
        socket.write_all(&[5, 255]).await.unwrap();
    });
    let result = MysqlDriver
        .connect(ConnectionOptions::Mysql {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: Some(SocksProxy {
                protocol: SocksProtocol::Socks5,
                host: "127.0.0.1".into(),
                port,
                username: Some("proxy-user".into()),
            }),
            proxy_secret: Some(Secret::new("proxy-password")),
            host: "database.example.invalid".into(),
            port: 3306,
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
    assert_eq!(
        result.err().expect("authentication rejection").kind,
        ErrorKind::Authentication
    );
    server.await.unwrap();
}

#[path = "../../driver-api/tests/support/socks_proxy.rs"]
mod socks_proxy;
#[tokio::test]
#[ignore = "requires disposable MySQL TLS fixture"]
async fn verified_tls_queries_cancellation_and_object_reads_use_proxy() {
    use std::{sync::atomic::Ordering, time::Duration};
    let port = std::env::var("CHOSCORDB_MYSQL_TLS_PORT")
        .unwrap()
        .parse()
        .unwrap();
    let (proxy, server, count) = socks_proxy::start(port).await;
    let mut connection = MysqlDriver
        .connect(ConnectionOptions::Mysql {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: Some(proxy),
            proxy_secret: Some(Secret::new("secret")),
            host: "localhost".into(),
            port,
            database: "".into(),
            user: "root".into(),
            password: Some(Secret::new("choscordb-test-password")),
            ssh: None,
            ssh_secret: None,
            tls: TlsMode::VerifyFull,
            root_certificate: Some(
                std::env::var_os("CHOSCORDB_MYSQL_TLS_ROOT_CERTIFICATE")
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
                    .execute("SELECT SLEEP(30)", QueryOptions::default())
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
    let mut object = connection
        .open_object(
            &ObjectId(r#"["information_schema","SCHEMATA"]"#.into()),
            1024 * 1024,
        )
        .await
        .unwrap();
    assert!(
        !object
            .fetch_page(PageSize::default())
            .await
            .unwrap()
            .rows
            .is_empty()
    );
    object.close().await.unwrap();
    drop(object);
    connection.close().await.unwrap();
    drop(connection);
    assert!(
        count.load(Ordering::SeqCst) >= 3,
        "control/object transports must use the proxy too"
    );
    server.abort();
}

#[tokio::test]
#[ignore = "requires disposable MySQL TLS fixture"]
async fn proxy_cannot_replace_the_database_certificate_hostname() {
    use std::sync::atomic::Ordering;
    let port = std::env::var("CHOSCORDB_MYSQL_TLS_PORT")
        .unwrap()
        .parse()
        .unwrap();
    let (proxy, server, count) = socks_proxy::start_for_host(port, "wrong-cert.invalid").await;
    let result = MysqlDriver
        .connect(ConnectionOptions::Mysql {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: Some(proxy),
            proxy_secret: Some(Secret::new("secret")),
            host: "wrong-cert.invalid".into(),
            port,
            database: "".into(),
            user: "root".into(),
            password: Some(Secret::new("choscordb-test-password")),
            ssh: None,
            ssh_secret: None,
            tls: TlsMode::VerifyFull,
            root_certificate: Some(
                std::env::var_os("CHOSCORDB_MYSQL_TLS_ROOT_CERTIFICATE")
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
