use choscordb_driver_api::*;
use choscordb_driver_mysql::MysqlDriver;
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

fn options(host: String, port: u16, user: &str) -> ConnectionOptions {
    ConnectionOptions::Mysql {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host,
        port,
        database: String::new(),
        user: user.into(),
        password: None,
        ssh_secret: None,
        tls: TlsMode::Disable,
        tls_identity: None,
        root_certificate: None,
        ssh: None,
    }
}

#[tokio::test]
async fn anonymous_and_explicit_usernames_are_sent_unchanged() {
    for user in ["", " literal user "] {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let opts = options(
            "127.0.0.1".into(),
            listener.local_addr().unwrap().port(),
            user,
        );
        let attempt = tokio::spawn(async { MysqlDriver.connect(opts).await });
        let (mut socket, _) = tokio::time::timeout(Duration::from_secs(2), listener.accept())
            .await
            .expect("connection must reach server")
            .unwrap();
        // Protocol 10 greeting with CLIENT_PROTOCOL_41 and CLIENT_SECURE_CONNECTION.
        let mut greeting = vec![10];
        greeting.extend_from_slice(b"8.0.0\0");
        greeting.extend_from_slice(&1u32.to_le_bytes());
        greeting.extend_from_slice(b"12345678\0");
        greeting.extend_from_slice(&0x8200u16.to_le_bytes());
        greeting.push(45);
        greeting.extend_from_slice(&2u16.to_le_bytes());
        greeting.extend_from_slice(&0u16.to_le_bytes());
        greeting.push(0);
        greeting.extend_from_slice(&[0; 10]);
        greeting.extend_from_slice(b"abcdefghijkl\0");
        let length = greeting.len() as u32;
        socket
            .write_all(&[length as u8, (length >> 8) as u8, (length >> 16) as u8, 0])
            .await
            .unwrap();
        socket.write_all(&greeting).await.unwrap();
        let mut header = [0; 4];
        socket.read_exact(&mut header).await.unwrap();
        assert_eq!(header[3], 1);
        let length = u32::from_le_bytes([header[0], header[1], header[2], 0]) as usize;
        let mut response = vec![0; length];
        socket.read_exact(&mut response).await.unwrap();
        assert_eq!(
            response[32..].split(|b| *b == 0).next().unwrap(),
            user.as_bytes()
        );
        attempt.abort();
    }
}

#[cfg(unix)]
#[tokio::test]
async fn explicit_unix_file_reaches_mysql_socket() {
    let directory = tempfile::tempdir_in("/tmp").unwrap();
    let path = directory.path().join("mysql.sock");
    let listener = tokio::net::UnixListener::bind(&path).unwrap();
    let opts = options(path.to_str().unwrap().into(), 3306, "");
    let attempt = tokio::spawn(async { MysqlDriver.connect(opts).await });
    let (_socket, _) = tokio::time::timeout(Duration::from_secs(2), listener.accept())
        .await
        .expect("connection must use explicit Unix socket file")
        .unwrap();
    attempt.abort();
}

#[tokio::test]
async fn unix_socket_rejects_tls_and_ssh_without_silent_downgrade() {
    for tunneled in [false, true] {
        let mut opts = options("/tmp/choscordb-missing.sock".into(), 5432, "db");
        if let ConnectionOptions::Mysql { tls, ssh, .. } = &mut opts {
            if tunneled {
                *ssh = Some(SshTunnel {
                    options: Default::default(),
                    host: "127.0.0.1".into(),
                    port: 22,
                    user: "operator".into(),
                    authentication: SshAuthentication::Agent,
                    identity_source: Default::default(),
                    identity_file: None,
                });
            } else {
                *tls = TlsMode::VerifyFull;
            }
        }
        let error = MysqlDriver
            .connect(opts)
            .await
            .err()
            .expect("unsupported transport combination");
        assert_eq!(error.kind, ErrorKind::InvalidInput);
    }
}
