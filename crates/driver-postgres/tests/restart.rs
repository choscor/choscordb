//! Exclusive crash/restart regression for the repository-owned disposable fixture.
use choscordb_driver_api::{
    ConnectionOptions, DatabaseDriver, ErrorKind, QueryOptions, Secret, TlsMode,
};
use choscordb_driver_postgres::PostgresDriver;
use std::{path::PathBuf, process::Command, time::Duration};

fn value(name: &str) -> String {
    std::env::var(name).expect("repository PostgreSQL fixture environment required")
}

fn settings() -> ConnectionOptions {
    ConnectionOptions::Postgres {
        host: value("CHOSCORDB_TEST_POSTGRES_HOST"),
        port: value("CHOSCORDB_TEST_POSTGRES_PORT").parse().unwrap(),
        database: value("CHOSCORDB_TEST_POSTGRES_DATABASE"),
        user: value("CHOSCORDB_TEST_POSTGRES_USER"),
        password: Some(Secret::new(value("CHOSCORDB_TEST_POSTGRES_PASSWORD"))),
        ssh_secret: None,
        tls: TlsMode::VerifyFull,
        ssh: None,
        root_certificate: Some(value("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE").into()),
    }
}

fn control(action: &str) {
    let ctl = PathBuf::from(value("CHOSCORDB_TEST_POSTGRES_CTL"));
    let data = PathBuf::from(value("CHOSCORDB_TEST_POSTGRES_DATA"));
    let marker = data.parent().unwrap().join("fixture.json");
    let ownership = std::fs::read_to_string(marker).expect("fixture ownership marker");
    assert!(ownership.contains("choscordb-postgres-test-fixture-v1"));
    let mut command = Command::new(ctl);
    command.arg("-D").arg(data);
    if action == "stop" {
        command.args(["-m", "immediate", "-w", "stop"]);
    } else {
        command
            .arg("-l")
            .arg(value("CHOSCORDB_TEST_POSTGRES_LOG"))
            .args(["-w", "start"]);
    }
    assert!(command.status().expect("run pg_ctl").success());
}

struct RestartOnDrop(bool);
impl Drop for RestartOnDrop {
    fn drop(&mut self) {
        if self.0 {
            control("start");
        }
    }
}

#[tokio::test]
#[ignore = "requires exclusive repository-owned PostgreSQL fixture"]
async fn active_connection_fails_terminally_and_fresh_connection_works_after_restart() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    let mut restart = RestartOnDrop(false);
    control("stop");
    restart.0 = true;

    let error = connection
        .execute("SELECT 1", QueryOptions::default())
        .await
        .err()
        .expect("stopped server must fail the active connection");
    assert!(matches!(
        error.kind,
        ErrorKind::Disconnected | ErrorKind::Connection
    ));
    let repeated = connection
        .execute("SELECT 2", QueryOptions::default())
        .await
        .err()
        .expect("lost connection must stay terminal");
    assert_eq!(repeated.kind, ErrorKind::Disconnected);

    control("start");
    restart.0 = false;
    let deadline = tokio::time::Instant::now() + Duration::from_secs(10);
    loop {
        match PostgresDriver.connect(settings()).await {
            Ok(mut fresh) => {
                fresh.close().await.unwrap();
                break;
            }
            Err(_) if tokio::time::Instant::now() < deadline => {
                tokio::time::sleep(Duration::from_millis(50)).await;
            }
            Err(error) => panic!("fresh connection failed after restart: {error}"),
        }
    }
}
