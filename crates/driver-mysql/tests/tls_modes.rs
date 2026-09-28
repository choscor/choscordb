//! Self-contained protocol tests run locally; ignored integration tests require
//! the disposable MySQL TLS fixture documented in mysql.md.
use choscordb_driver_api::*;
use choscordb_driver_mysql::MysqlDriver;
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::{TcpListener, TcpStream},
};

async fn send_packet(stream: &mut TcpStream, sequence: u8, body: &[u8]) {
    let length = body.len();
    stream
        .write_all(&[
            length as u8,
            (length >> 8) as u8,
            (length >> 16) as u8,
            sequence,
        ])
        .await
        .unwrap();
    stream.write_all(body).await.unwrap();
}

async fn read_packet(stream: &mut TcpStream) -> Vec<u8> {
    let mut header = [0; 4];
    stream.read_exact(&mut header).await.unwrap();
    let length =
        usize::from(header[0]) | (usize::from(header[1]) << 8) | (usize::from(header[2]) << 16);
    let mut body = vec![0; length];
    stream.read_exact(&mut body).await.unwrap();
    body
}

fn handshake(tls: bool) -> Vec<u8> {
    let capabilities = 0x0000_a205_u32 | if tls { 0x800 } else { 0 };
    let mut body = vec![10];
    body.extend_from_slice(b"8.0.36\0");
    body.extend_from_slice(&1_u32.to_le_bytes());
    body.extend_from_slice(b"12345678\0");
    body.extend_from_slice(&(capabilities as u16).to_le_bytes());
    body.push(45);
    body.extend_from_slice(&2_u16.to_le_bytes());
    body.extend_from_slice(&((capabilities >> 16) as u16).to_le_bytes());
    body.push(21);
    body.extend_from_slice(&[0; 10]);
    body.extend_from_slice(b"abcdefghijkl\0");
    body.extend_from_slice(b"mysql_native_password\0");
    body
}

fn prefer_options(port: u16) -> ConnectionOptions {
    ConnectionOptions::Mysql {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host: "127.0.0.1".into(),
        port,
        database: "".into(),
        user: "tester".into(),
        password: None,
        ssh_secret: None,
        ssh: None,
        root_certificate: None,
        tls_identity: None,
        tls: TlsMode::Prefer,
    }
}

fn options(mode: TlsMode, trust: bool) -> ConnectionOptions {
    ConnectionOptions::Mysql {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host: "127.0.0.1".into(),
        port: std::env::var("CHOSCORDB_MYSQL_TLS_PORT")
            .expect("isolated TLS fixture")
            .parse()
            .unwrap(),
        database: "".into(),
        user: "root".into(),
        password: Some(Secret::new("choscordb-test-password")),
        ssh: None,
        ssh_secret: None,
        tls: mode,
        tls_identity: None,
        root_certificate: trust.then(|| {
            std::env::var_os("CHOSCORDB_MYSQL_TLS_ROOT_CERTIFICATE")
                .unwrap()
                .into()
        }),
    }
}
#[tokio::test]
#[ignore = "requires disposable MySQL TLS fixture"]
async fn require_uses_tls_without_trust_and_verify_ca_enforces_trust() {
    for mode in [TlsMode::Require, TlsMode::VerifyCa] {
        let trust = mode == TlsMode::VerifyCa;
        let mut connection = MysqlDriver.connect(options(mode, trust)).await.unwrap();
        let mut cursor = connection
            .execute("SHOW STATUS LIKE 'Ssl_cipher'", QueryOptions::default())
            .await
            .unwrap();
        let page = cursor.fetch_page(PageSize::default()).await.unwrap();
        assert!(
            matches!(&page.rows[0][1], Value::Text(cipher) if !cipher.is_empty()),
            "transport must be encrypted"
        );
        cursor.close().await.unwrap();
        connection.close().await.unwrap();
    }
    assert!(
        MysqlDriver
            .connect(options(TlsMode::VerifyCa, false))
            .await
            .is_err()
    );
    assert!(
        MysqlDriver
            .connect(options(TlsMode::VerifyFull, true))
            .await
            .is_err()
    );
    let mut verified = options(TlsMode::VerifyFull, true);
    if let ConnectionOptions::Mysql { host, .. } = &mut verified {
        *host = "localhost".into();
    }
    MysqlDriver
        .connect(verified)
        .await
        .unwrap()
        .close()
        .await
        .unwrap();
}

#[tokio::test]
async fn prefer_falls_back_when_server_does_not_offer_tls() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        for attempt in 0..2 {
            let (mut stream, _) = listener.accept().await.unwrap();
            send_packet(&mut stream, 0, &handshake(false)).await;
            if attempt == 1 {
                let response = read_packet(&mut stream).await;
                assert_eq!(response[1] & 0x08, 0, "fallback must omit CLIENT_SSL");
                send_packet(&mut stream, 2, &[0, 0, 0, 2, 0, 0, 0]).await;
                let _ = read_packet(&mut stream).await;
            }
        }
    });
    let mut connection = tokio::time::timeout(
        std::time::Duration::from_secs(3),
        MysqlDriver.connect(prefer_options(port)),
    )
    .await
    .unwrap()
    .expect("plaintext fallback should connect");
    connection.close().await.unwrap();
    tokio::time::timeout(std::time::Duration::from_secs(3), server)
        .await
        .unwrap()
        .unwrap();
}

#[tokio::test]
async fn prefer_does_not_downgrade_after_tls_handshake_fails() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut stream, _) = listener.accept().await.unwrap();
        send_packet(&mut stream, 0, &handshake(true)).await;
        let ssl_request = read_packet(&mut stream).await;
        assert_ne!(ssl_request[1] & 0x08, 0, "client should request TLS");
        drop(stream);
        assert!(
            tokio::time::timeout(std::time::Duration::from_millis(250), listener.accept())
                .await
                .is_err(),
            "a failed TLS handshake must not trigger plaintext retry"
        );
    });
    let result = tokio::time::timeout(
        std::time::Duration::from_secs(3),
        MysqlDriver.connect(prefer_options(port)),
    )
    .await
    .unwrap();
    assert_eq!(result.err().unwrap().kind, ErrorKind::Tls);
    server.await.unwrap();
}

#[tokio::test]
async fn prefer_retries_tls_for_later_object_transport() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut first, _) = listener.accept().await.unwrap();
        send_packet(&mut first, 0, &handshake(false)).await;
        drop(first);

        let (mut session, _) = listener.accept().await.unwrap();
        send_packet(&mut session, 0, &handshake(false)).await;
        let response = read_packet(&mut session).await;
        assert_eq!(response[1] & 0x08, 0);
        send_packet(&mut session, 2, &[0, 0, 0, 2, 0, 0, 0]).await;

        let (mut object, _) = listener.accept().await.unwrap();
        send_packet(&mut object, 0, &handshake(true)).await;
        let request = read_packet(&mut object).await;
        let requested_tls = request[1] & 0x08 != 0;
        drop(object);
        requested_tls
    });
    let mut connection = tokio::time::timeout(
        std::time::Duration::from_secs(3),
        MysqlDriver.connect(prefer_options(port)),
    )
    .await
    .unwrap()
    .unwrap();
    let _ = tokio::time::timeout(
        std::time::Duration::from_secs(3),
        connection.open_object(&ObjectId(r#"["db","table"]"#.into()), 1024),
    )
    .await
    .unwrap();
    assert!(
        server.await.unwrap(),
        "later object transport must try TLS anew"
    );
}

#[tokio::test]
#[ignore = "requires disposable MySQL TLS fixture"]
async fn certificate_authentication_requires_the_correct_client_identity() {
    let identity = std::env::var_os("CHOSCORDB_MYSQL_TLS_CLIENT_IDENTITY").unwrap();
    let password = std::env::var("CHOSCORDB_MYSQL_TLS_CLIENT_PASSWORD").unwrap();
    for passphrase in [None, Some("incorrect"), Some(password.as_str())] {
        let mut settings = options(TlsMode::VerifyCa, true);
        if let ConnectionOptions::Mysql {
            user,
            password,
            tls_identity,
            ..
        } = &mut settings
        {
            *user = "tls_client".into();
            *password = Some(Secret::new("fixture-password"));
            *tls_identity = passphrase.map(|p| TlsIdentity {
                path: identity.clone().into(),
                password: Some(Secret::new(p)),
            });
        }
        let result = MysqlDriver.connect(settings).await;
        if passphrase == Some(password.as_str()) {
            result
                .expect("valid client certificate must authenticate")
                .close()
                .await
                .unwrap();
        } else {
            assert!(
                result.is_err(),
                "missing or inaccessible client identity must fail"
            );
        }
    }
}
