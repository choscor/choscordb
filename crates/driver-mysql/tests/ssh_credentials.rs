use choscordb_driver_api::{
    ConnectionOptions, DatabaseDriver, ErrorKind, Secret, SshAuthentication, SshTunnel, TlsMode,
};
use choscordb_driver_mysql::MysqlDriver;

fn options(authentication: SshAuthentication, secret: Option<&str>) -> ConnectionOptions {
    ConnectionOptions::Mysql {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host: "127.0.0.1".into(),
        port: 3306,
        database: "test".into(),
        user: "tester".into(),
        password: None,
        ssh_secret: secret.map(Secret::new),
        tls: TlsMode::Disable,
        tls_identity: None,
        root_certificate: None,
        ssh: Some(SshTunnel {
            options: Default::default(),
            host: "127.0.0.1".into(),
            port: 22,
            user: "tester".into(),
            identity_source: Default::default(),
            identity_file: (authentication == SshAuthentication::PublicKey)
                .then(|| "/unused/key".into()),
            authentication,
        }),
    }
}

#[tokio::test]
async fn missing_and_empty_ssh_passwords_return_authentication_errors() {
    for secret in [Some(""), None] {
        let result = MysqlDriver
            .connect(options(SshAuthentication::Password, secret))
            .await;
        let error = result.err().expect("SSH password must be required");
        assert_eq!(error.kind, ErrorKind::Authentication);
    }
}

#[tokio::test]
async fn malformed_ssh_credentials_return_invalid_input() {
    for authentication in [SshAuthentication::Password, SshAuthentication::PublicKey] {
        for secret in ["line\nfeed", "carriage\rreturn", "null\0byte"] {
            let result = MysqlDriver
                .connect(options(authentication.clone(), Some(secret)))
                .await;
            let error = result.err().expect("SSH credential must be validated");
            assert_eq!(error.kind, ErrorKind::InvalidInput);
        }
    }
}
