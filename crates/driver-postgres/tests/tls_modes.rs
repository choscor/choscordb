//! Requires the isolated PostgreSQL fixture and its localhost server certificate.
use choscordb_driver_api::*;
use choscordb_driver_postgres::PostgresDriver;

fn options(mode: TlsMode, host: &str, trust: bool) -> ConnectionOptions {
    let config: tokio_postgres::Config = std::env::var("CHOSCORDB_TEST_POSTGRES")
        .expect("isolated fixture")
        .parse()
        .unwrap();
    ConnectionOptions::Postgres {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host: host.into(),
        port: config.get_ports()[0],
        database: "postgres".into(),
        user: config.get_user().unwrap().into(),
        password: config
            .get_password()
            .map(|p| Secret::new(std::str::from_utf8(p).unwrap())),
        ssh_secret: None,
        ssh: None,
        tls: mode,
        tls_identity: None,
        root_certificate: trust.then(|| {
            std::env::var_os("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE")
                .unwrap()
                .into()
        }),
    }
}

#[tokio::test]
#[ignore = "requires disposable PostgreSQL fixture"]
async fn require_encrypts_without_requiring_a_trusted_certificate() {
    let mut connection = PostgresDriver
        .connect(options(TlsMode::Require, "127.0.0.1", false))
        .await
        .unwrap();
    let mut cursor = connection
        .execute(
            "SELECT ssl FROM pg_stat_ssl WHERE pid = pg_backend_pid()",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Bool(true)]]
    );
    cursor.close().await.unwrap();
    connection.close().await.unwrap();
}

#[tokio::test]
#[ignore = "requires disposable PostgreSQL fixture"]
async fn verify_ca_checks_trust_but_not_hostname_and_verify_full_checks_both() {
    let mut connection = PostgresDriver
        .connect(options(TlsMode::VerifyCa, "127.0.0.1", true))
        .await
        .unwrap();
    connection.close().await.unwrap();
    assert!(
        PostgresDriver
            .connect(options(TlsMode::VerifyCa, "127.0.0.1", false))
            .await
            .is_err()
    );
    assert!(
        PostgresDriver
            .connect(options(TlsMode::VerifyFull, "127.0.0.1", true))
            .await
            .is_err()
    );
    let mut connection = PostgresDriver
        .connect(options(TlsMode::VerifyFull, "localhost", true))
        .await
        .unwrap();
    connection.close().await.unwrap();
}

#[tokio::test]
async fn prefer_falls_back_when_server_declines_tls_but_require_does_not() {
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    for mode in [TlsMode::Prefer, TlsMode::Require] {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = listener.local_addr().unwrap().port();
        let server = tokio::spawn(async move {
            let (mut socket, _) = listener.accept().await.unwrap();
            let mut ssl_request = [0; 8];
            socket.read_exact(&mut ssl_request).await.unwrap();
            assert_eq!(ssl_request, [0, 0, 0, 8, 4, 210, 22, 47]);
            socket.write_all(b"N").await.unwrap();
            let mut length = [0; 4];
            if socket.read_exact(&mut length).await.is_err() {
                return false;
            }
            let mut startup = vec![0; u32::from_be_bytes(length) as usize - 4];
            socket.read_exact(&mut startup).await.unwrap();
            assert_eq!(&startup[..4], &[0, 3, 0, 0]);
            socket
                .write_all(b"R\0\0\0\x08\0\0\0\0Z\0\0\0\x05I")
                .await
                .unwrap();
            let mut terminate = [0; 5];
            let _ = socket.read_exact(&mut terminate).await;
            true
        });
        let result = PostgresDriver
            .connect(ConnectionOptions::Postgres {
                ssh_jump_secrets: Default::default(),
                ssh_private_key: None,
                ssh_jump_private_keys: Default::default(),
                proxy: None,
                proxy_secret: None,
                host: "127.0.0.1".into(),
                port,
                database: "test".into(),
                user: "tester".into(),
                password: None,
                ssh_secret: None,
                ssh: None,
                root_certificate: None,
                tls_identity: None,
                tls: mode.clone(),
            })
            .await;
        if mode == TlsMode::Prefer {
            result
                .expect("Prefer must support a plaintext-only server")
                .close()
                .await
                .unwrap();
            assert!(server.await.unwrap());
        } else {
            assert!(result.is_err(), "Require must refuse plaintext");
            assert!(!server.await.unwrap());
        }
    }
}

#[tokio::test]
#[ignore = "requires disposable PostgreSQL fixture"]
async fn certificate_authentication_requires_the_correct_client_identity() {
    let identity = std::env::var_os("CHOSCORDB_TEST_POSTGRES_CLIENT_IDENTITY").unwrap();
    let password = std::env::var("CHOSCORDB_TEST_POSTGRES_CLIENT_PASSWORD").unwrap();
    for passphrase in [None, Some("incorrect"), Some(password.as_str())] {
        let mut settings = options(TlsMode::VerifyFull, "localhost", true);
        if let ConnectionOptions::Postgres {
            user,
            password,
            tls_identity,
            ..
        } = &mut settings
        {
            *user = "tls_client".into();
            *password = None;
            *tls_identity = passphrase.map(|p| TlsIdentity {
                path: identity.clone().into(),
                password: Some(Secret::new(p)),
            });
        }
        let result = PostgresDriver.connect(settings).await;
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

#[tokio::test]
#[ignore = "requires disposable PostgreSQL fixture"]
async fn root_certificate_bundle_loads_every_certificate() {
    use std::io::Write;
    let root = std::env::var_os("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE").unwrap();
    let client = std::path::PathBuf::from(
        std::env::var_os("CHOSCORDB_TEST_POSTGRES_CLIENT_IDENTITY").unwrap(),
    )
    .with_extension("crt");
    let mut bundle = tempfile::NamedTempFile::new().unwrap();
    bundle.write_all(&std::fs::read(client).unwrap()).unwrap();
    bundle.write_all(&std::fs::read(root).unwrap()).unwrap();
    let mut settings = options(TlsMode::VerifyFull, "localhost", true);
    if let ConnectionOptions::Postgres {
        root_certificate, ..
    } = &mut settings
    {
        *root_certificate = Some(bundle.path().into());
    }
    PostgresDriver
        .connect(settings)
        .await
        .expect("CA after another certificate must be loaded")
        .close()
        .await
        .unwrap();
}
