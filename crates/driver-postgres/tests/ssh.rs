use choscordb_driver_api::*;
use choscordb_driver_postgres::PostgresDriver;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

#[tokio::test]
async fn ssh_connection_never_falls_back_to_direct_database() {
    let database = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = database.local_addr().unwrap().port();
    let server = tokio::spawn(async move {
        let (mut socket, _) = database.accept().await.unwrap();
        let length = socket.read_u32().await.unwrap();
        let mut startup = vec![0; length as usize - 4];
        socket.read_exact(&mut startup).await.unwrap();
        socket
            .write_all(b"R\0\0\0\x08\0\0\0\0Z\0\0\0\x05I")
            .await
            .unwrap();
        let mut buffer = [0; 1024];
        let _ = socket.read(&mut buffer).await;
    });
    let unavailable = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let ssh_port = unavailable.local_addr().unwrap().port();
    drop(unavailable);
    let result = PostgresDriver
        .connect(ConnectionOptions::Postgres {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: None,
            proxy_secret: None,
            host: "127.0.0.1".into(),
            port,
            database: "postgres".into(),
            user: "test".into(),
            password: None,
            ssh_secret: None,
            tls: TlsMode::Disable,
            tls_identity: None,
            root_certificate: None,
            ssh: Some(SshTunnel {
                options: Default::default(),
                host: "127.0.0.1".into(),
                port: ssh_port,
                user: "test".into(),
                authentication: SshAuthentication::Agent,
                identity_source: Default::default(),
                identity_file: None,
            }),
        })
        .await;
    assert!(
        matches!(result, Err(ref error) if error.kind == ErrorKind::Connection),
        "SSH failure must not connect directly"
    );
    assert!(
        !server.is_finished(),
        "database received a direct connection"
    );
    server.abort();
}

#[tokio::test]
async fn ssh_password_authentication_requires_a_secret() {
    let result = PostgresDriver
        .connect(ConnectionOptions::Postgres {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: None,
            proxy_secret: None,
            host: "db.internal".into(),
            port: 5432,
            database: "postgres".into(),
            user: "test".into(),
            password: None,
            ssh_secret: None,
            tls: TlsMode::Disable,
            tls_identity: None,
            root_certificate: None,
            ssh: Some(SshTunnel {
                options: Default::default(),
                host: "bastion.example".into(),
                port: 22,
                user: "operator".into(),
                authentication: SshAuthentication::Password,
                identity_source: Default::default(),
                identity_file: None,
            }),
        })
        .await;
    assert!(matches!(
        result,
        Err(DriverError {
            kind: ErrorKind::Authentication,
            ..
        })
    ));
}
