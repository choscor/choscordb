use choscordb_driver_api::*;
use choscordb_driver_postgres::PostgresDriver;
use std::time::Duration;
use tokio::io::AsyncReadExt;

fn options(host: String, port: u16, database: &str) -> ConnectionOptions {
    ConnectionOptions::Postgres {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host,
        port,
        database: database.into(),
        user: "operator".into(),
        password: None,
        ssh_secret: None,
        tls: TlsMode::Disable,
        tls_identity: None,
        root_certificate: None,
        ssh: None,
    }
}

#[tokio::test]
async fn omitted_database_uses_username_and_explicit_database_is_preserved() {
    for (database, expected) in [("", "operator"), ("literal database", "literal database")] {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let opts = options(
            "127.0.0.1".into(),
            listener.local_addr().unwrap().port(),
            database,
        );
        let attempt = tokio::spawn(async { PostgresDriver.connect(opts).await });
        let (mut socket, _) = tokio::time::timeout(Duration::from_secs(2), listener.accept())
            .await
            .expect("connection must reach server")
            .unwrap();
        let size = socket.read_u32().await.unwrap();
        let mut bytes = vec![0; size as usize - 4];
        socket.read_exact(&mut bytes).await.unwrap();
        assert_eq!(&bytes[..4], &[0, 3, 0, 0]);
        let fields: Vec<_> = bytes[4..].split(|b| *b == 0).collect();
        let value = fields
            .chunks(2)
            .find(|field| field[0] == b"database")
            .unwrap()[1];
        assert_eq!(value, expected.as_bytes());
        attempt.abort();
    }
}

#[cfg(unix)]
#[tokio::test]
async fn explicit_unix_directory_reaches_postgres_socket() {
    let directory = tempfile::tempdir_in("/tmp").unwrap();
    let listener = tokio::net::UnixListener::bind(directory.path().join(".s.PGSQL.5432")).unwrap();
    let opts = options(
        directory.path().to_str().unwrap().into(),
        5432,
        "local_database",
    );
    let attempt = tokio::spawn(async { PostgresDriver.connect(opts).await });
    let (mut socket, _) = tokio::time::timeout(Duration::from_secs(2), listener.accept())
        .await
        .expect("connection must use Unix socket directory")
        .unwrap();
    let size = socket.read_u32().await.unwrap();
    let mut bytes = vec![0; size as usize - 4];
    socket.read_exact(&mut bytes).await.unwrap();
    assert_eq!(&bytes[..4], &[0, 3, 0, 0]);
    assert!(
        bytes
            .windows(24)
            .any(|w| w == b"database\0local_database\0")
    );
    attempt.abort();
}

#[tokio::test]
async fn unix_socket_rejects_tls_and_ssh_without_silent_downgrade() {
    for tunneled in [false, true] {
        let mut opts = options("/tmp/choscordb-missing.sock".into(), 5432, "db");
        if let ConnectionOptions::Postgres { tls, ssh, .. } = &mut opts {
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
        let error = PostgresDriver
            .connect(opts)
            .await
            .err()
            .expect("unsupported transport combination");
        assert_eq!(error.kind, ErrorKind::InvalidInput);
    }
}
