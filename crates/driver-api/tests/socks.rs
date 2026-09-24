use choscordb_driver_api::*;
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
fn proxy(port: u16) -> SocksProxy {
    SocksProxy {
        protocol: SocksProtocol::Socks5,
        host: "127.0.0.1".into(),
        port,
        username: None,
    }
}
#[tokio::test]
async fn socks5_keeps_destination_dns_remote_and_returns_bidirectional_stream() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut socket, _) = listener.accept().await.unwrap();
        let mut greeting = [0; 3];
        socket.read_exact(&mut greeting).await.unwrap();
        assert_eq!(greeting, [5, 1, 0]);
        socket.write_all(&[5, 0]).await.unwrap();
        let mut request = [0; 5];
        socket.read_exact(&mut request).await.unwrap();
        assert_eq!(request, [5, 1, 0, 3, 18]);
        let mut address = [0; 20];
        socket.read_exact(&mut address).await.unwrap();
        assert_eq!(&address[..18], b"db.example.invalid");
        assert_eq!(&address[18..], [0x15, 0x38]);
        socket
            .write_all(&[5, 0, 0, 1, 127, 0, 0, 1, 0, 0])
            .await
            .unwrap();
        let mut message = [0; 4];
        socket.read_exact(&mut message).await.unwrap();
        assert_eq!(&message, b"ping");
        socket.write_all(b"pong").await.unwrap();
    });
    let mut stream = connect_socks(
        &proxy(port),
        None,
        "db.example.invalid",
        5432,
        Duration::from_secs(2),
    )
    .await
    .unwrap();
    stream.write_all(b"ping").await.unwrap();
    let mut response = [0; 4];
    stream.read_exact(&mut response).await.unwrap();
    assert_eq!(&response, b"pong");
    server.await.unwrap();
}

#[tokio::test]
async fn socks5_password_authentication_and_ipv6_destination_are_literal() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut socket, _) = listener.accept().await.unwrap();
        let mut greeting = [0; 3];
        socket.read_exact(&mut greeting).await.unwrap();
        assert_eq!(greeting, [5, 1, 2]);
        socket.write_all(&[5, 2]).await.unwrap();
        let mut credentials = [0; 12];
        socket.read_exact(&mut credentials).await.unwrap();
        assert_eq!(&credentials, b"\x01\x05alice\x04p@ss");
        socket.write_all(&[1, 0]).await.unwrap();
        let mut request = [0; 22];
        socket.read_exact(&mut request).await.unwrap();
        assert_eq!(&request[..4], [5, 1, 0, 4]);
        assert_eq!(
            &request[4..20],
            &[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]
        );
        assert_eq!(&request[20..], [0x0c, 0xea]);
        socket
            .write_all(&[5, 0, 0, 3, 3, b'f', b'o', b'o', 0, 0])
            .await
            .unwrap();
    });
    let mut settings = proxy(port);
    settings.username = Some("alice".into());
    let secret = Secret::new("p@ss");
    connect_socks(
        &settings,
        Some(&secret),
        "[::1]",
        3306,
        Duration::from_secs(2),
    )
    .await
    .unwrap();
    server.await.unwrap();
}

#[tokio::test]
async fn socks4a_sends_user_id_and_domain_without_local_dns() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut socket, _) = listener.accept().await.unwrap();
        let mut request = [0; 33];
        socket.read_exact(&mut request).await.unwrap();
        assert_eq!(&request[..8], [4, 1, 0x15, 0x38, 0, 0, 0, 1]);
        assert_eq!(&request[8..], b"alice\0db.example.invalid\0");
        socket.write_all(&[0, 90, 0, 0, 0, 0, 0, 0]).await.unwrap();
    });
    let settings = SocksProxy {
        protocol: SocksProtocol::Socks4,
        username: Some("alice".into()),
        ..proxy(port)
    };
    connect_socks(
        &settings,
        None,
        "db.example.invalid",
        5432,
        Duration::from_secs(2),
    )
    .await
    .unwrap();
    server.await.unwrap();
}

#[tokio::test]
async fn configured_authentication_cannot_downgrade_and_errors_hide_passwords() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut socket, _) = listener.accept().await.unwrap();
        let mut greeting = [0; 3];
        socket.read_exact(&mut greeting).await.unwrap();
        assert_eq!(greeting, [5, 1, 2]);
        socket.write_all(&[5, 0]).await.unwrap();
        let mut byte = [0];
        assert_eq!(socket.read(&mut byte).await.unwrap(), 0);
    });
    let mut settings = proxy(port);
    settings.username = Some("alice".into());
    let error = connect_socks(
        &settings,
        Some(&Secret::new("do-not-expose")),
        "localhost",
        5432,
        Duration::from_secs(2),
    )
    .await
    .unwrap_err();
    assert_eq!(error.kind, ErrorKind::Authentication);
    assert!(!format!("{error:?} {error}").contains("do-not-expose"));
    server.await.unwrap();
}
#[tokio::test]
async fn stalled_proxy_handshake_times_out_and_closes_transport() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut socket, _) = listener.accept().await.unwrap();
        let mut greeting = [0; 3];
        socket.read_exact(&mut greeting).await.unwrap();
        socket.write_all(&[5]).await.unwrap();
        let mut byte = [0];
        assert_eq!(socket.read(&mut byte).await.unwrap(), 0);
    });
    let error = connect_socks(
        &proxy(port),
        None,
        "localhost",
        5432,
        Duration::from_millis(50),
    )
    .await
    .unwrap_err();
    assert_eq!(error.kind, ErrorKind::Timeout);
    tokio::time::timeout(Duration::from_secs(1), server)
        .await
        .unwrap()
        .unwrap();
}
#[tokio::test]
async fn invalid_proxy_auth_and_socks4_ipv6_fail_before_network_activity() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let mut settings = proxy(port);
    settings.username = Some("alice".into());
    for secret in [
        None,
        Some(Secret::new("")),
        Some(Secret::new("x".repeat(256))),
    ] {
        assert_eq!(
            connect_socks(
                &settings,
                secret.as_ref(),
                "localhost",
                5432,
                Duration::from_secs(1)
            )
            .await
            .unwrap_err()
            .kind,
            ErrorKind::Authentication
        );
    }
    settings.protocol = SocksProtocol::Socks4;
    assert_eq!(
        connect_socks(&settings, None, "::1", 5432, Duration::from_secs(1))
            .await
            .unwrap_err()
            .kind,
        ErrorKind::InvalidInput
    );
    assert!(
        tokio::time::timeout(Duration::from_millis(50), listener.accept())
            .await
            .is_err()
    );
}

#[tokio::test]
async fn malformed_proxy_reply_is_a_connection_failure_not_invalid_user_input() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut socket, _) = listener.accept().await.unwrap();
        let mut greeting = [0; 3];
        socket.read_exact(&mut greeting).await.unwrap();
        socket.write_all(&[5, 0]).await.unwrap();
        let mut request = [0; 10];
        socket.read_exact(&mut request).await.unwrap();
        socket.write_all(&[5, 0, 0, 9]).await.unwrap();
    });
    assert_eq!(
        connect_socks(
            &proxy(port),
            None,
            "127.0.0.1",
            5432,
            Duration::from_secs(1)
        )
        .await
        .unwrap_err()
        .kind,
        ErrorKind::Connection
    );
    server.await.unwrap();
}
