use choscordb_driver_api::*;
use choscordb_driver_postgres::PostgresDriver;
fn settings() -> ConnectionOptions {
    let config: tokio_postgres::Config = std::env::var("CHOSCORDB_TEST_POSTGRES")
        .expect("CHOSCORDB_TEST_POSTGRES must name a disposable fixture")
        .parse()
        .unwrap();
    let fixture_host = config.get_hosts().first().expect("fixture TCP host");
    #[cfg(unix)]
    let host = match fixture_host {
        tokio_postgres::config::Host::Tcp(host) => host.clone(),
        tokio_postgres::config::Host::Unix(_) => panic!("fixture requires TCP"),
    };
    #[cfg(not(unix))]
    let host = {
        let tokio_postgres::config::Host::Tcp(host) = fixture_host;
        host.clone()
    };
    ConnectionOptions::Postgres {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host,
        port: config.get_ports().first().copied().unwrap_or(5432),
        database: config.get_dbname().unwrap_or("postgres").into(),
        user: config.get_user().expect("fixture user").into(),
        password: config
            .get_password()
            .map(|value| Secret::new(std::str::from_utf8(value).unwrap())),
        ssh_secret: None,
        tls: if std::env::var_os("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE").is_some() {
            TlsMode::VerifyFull
        } else {
            TlsMode::Disable
        },
        ssh: std::env::var("CHOSCORDB_TEST_POSTGRES_SSH_HOST")
            .ok()
            .map(|host| SshTunnel {
                options: Default::default(),
                host,
                port: std::env::var("CHOSCORDB_TEST_POSTGRES_SSH_PORT")
                    .expect("SSH fixture port")
                    .parse()
                    .unwrap(),
                user: std::env::var("CHOSCORDB_TEST_POSTGRES_SSH_USER").expect("SSH fixture user"),
                authentication: SshAuthentication::PublicKey,
                identity_source: Default::default(),
                identity_file: std::env::var("CHOSCORDB_TEST_POSTGRES_SSH_IDENTITY").ok(),
            }),
        tls_identity: None,
        root_certificate: std::env::var_os("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE")
            .map(Into::into),
    }
}

#[path = "../../driver-api/tests/support/driver_contract.rs"]
mod contract;

#[tokio::test]
#[ignore = "requires disposable postgres fixture"]
async fn bounded_results() {
    contract::bounded_results(&PostgresDriver, settings).await;
}

#[tokio::test]
#[ignore = "requires disposable postgres fixture"]
async fn close_rolls_back() {
    contract::close_rolls_back(&PostgresDriver, settings).await;
}

#[tokio::test]
#[ignore = "requires disposable postgres fixture"]
async fn cancellation_generation() {
    contract::cancellation_generation(&PostgresDriver, settings).await;
}

#[tokio::test]
#[ignore = "requires disposable postgres fixture"]
async fn schema_preflight_preserves_session() {
    contract::schema_preflight_preserves_session(&PostgresDriver, settings).await;
}
