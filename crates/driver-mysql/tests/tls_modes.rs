//! Requires the dedicated disposable MySQL TLS fixture documented in mysql.md.
use choscordb_driver_api::*;
use choscordb_driver_mysql::MysqlDriver;

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
async fn unsupported_prefer_policy_is_rejected_before_connecting() {
    let result = MysqlDriver
        .connect(ConnectionOptions::Mysql {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: None,
            proxy_secret: None,
            host: "127.0.0.1".into(),
            port: 1,
            database: "".into(),
            user: "tester".into(),
            password: None,
            ssh_secret: None,
            ssh: None,
            root_certificate: None,
            tls_identity: None,
            tls: TlsMode::Prefer,
        })
        .await;
    assert_eq!(
        result.err().expect("MySQL Prefer unsupported").kind,
        ErrorKind::InvalidInput
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
