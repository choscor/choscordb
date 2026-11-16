use choscordb_core::*;
use choscordb_driver_api::*;
use choscordb_driver_sqlite::SqliteDriver;
use std::{
    path::{Path, PathBuf},
    sync::Arc,
    time::{Duration, Instant},
};

fn database(dir: &Path, name: &str, table: &str) -> PathBuf {
    let path = dir.join(name);
    rusqlite::Connection::open(&path)
        .unwrap()
        .execute_batch(&format!("CREATE TABLE {table}(a INTEGER)"))
        .unwrap();
    path
}

fn sqlite(path: &Path) -> ConnectionOptions {
    ConnectionOptions::Sqlite {
        path: path.into(),
        read_only: false,
    }
}

fn profile(id: &str, path: &Path) -> ConnectionProfile {
    ConnectionProfile {
        id: id.into(),
        name: "Local".into(),
        group_id: None,
        configuration: ProfileConfiguration::Sqlite {
            path: path.to_string_lossy().into_owned(),
            read_only: false,
        },
        credential_ref: None,
        proxy_credential_ref: None,
        ssh_jump_credential_refs: Default::default(),
        ssh_jump_private_key_refs: Default::default(),
        environment: Default::default(),
        environment_label: None,
        color: Default::default(),
        safety: SafetyLevel::Silent,
        tls_credential_ref: None,
        ssh_credential_ref: None,
        ssh_private_key_ref: None,
        row_limit: 0,
    }
}

fn no_secrets() -> ProfileSecrets {
    ProfileSecrets {
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        ssh_jumps: Default::default(),
        database: None,
        ssh: None,
        tls: None,
        proxy: None,
    }
}

fn wait<T>(engine: &mut Engine, mut pick: impl FnMut(Event) -> Option<T>) -> T {
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        if let Some(found) = engine.try_event().and_then(&mut pick) {
            return found;
        }
        assert!(Instant::now() < deadline, "no expected event");
        std::thread::sleep(Duration::from_millis(2));
    }
}

fn connected(engine: &mut Engine, connection: ConnectionId) {
    wait(engine, |event| match event {
        Event::Connected { connection: c, .. } if c == connection => Some(()),
        Event::ConnectionFailed { error, .. } => panic!("{}", error.message),
        _ => None,
    })
}

fn switched(engine: &mut Engine, token: u64) -> std::result::Result<ConnectionId, String> {
    wait(engine, |event| match event {
        Event::DatabaseSwitched {
            connection,
            request_token,
            ..
        } if request_token == token => Some(Ok(connection)),
        Event::DatabaseSwitchFailed {
            request_token,
            error,
            ..
        } if request_token == token => Some(Err(error.message)),
        _ => None,
    })
}

fn tables(engine: &mut Engine, connection: ConnectionId) -> Vec<String> {
    let query = engine
        .execute(
            connection,
            "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name".into(),
            QueryOptions::default(),
        )
        .unwrap();
    engine.fetch_page_at(query, 0).unwrap();
    wait(engine, |event| match event {
        Event::Page { query: q, page, .. } | Event::StoredPage { query: q, page, .. }
            if q == query =>
        {
            Some(
                page.rows
                    .iter()
                    .map(|row| match &row[0] {
                        Value::Text(name) => name.to_string(),
                        other => panic!("{other:?}"),
                    })
                    .collect(),
            )
        }
        Event::QueryFailed { error, .. } => panic!("{}", error.message),
        _ => None,
    })
}

fn run(engine: &mut Engine, connection: ConnectionId, sql: &str) {
    let query = engine
        .execute(connection, sql.into(), QueryOptions::default())
        .unwrap();
    engine.fetch_page_at(query, 0).unwrap();
    wait(engine, |event| match event {
        Event::QueryFinished { query: q, .. } if q == query => Some(()),
        Event::QueryFailed { error, .. } => panic!("{}", error.message),
        _ => None,
    })
}

fn engine() -> Engine {
    Engine::new(EngineConfig::default(), vec![Arc::new(SqliteDriver)]).unwrap()
}

#[test]
fn a_connection_switches_to_another_database_and_keeps_its_id() {
    let dir = tempfile::tempdir().unwrap();
    let first = database(dir.path(), "first.sqlite", "only_first");
    let second = database(dir.path(), "second.sqlite", "only_second");
    let mut engine = engine();
    let connection = engine.connect("sqlite", sqlite(&first)).unwrap();
    connected(&mut engine, connection);
    assert_eq!(tables(&mut engine, connection), ["only_first"]);
    engine
        .switch_database(connection, sqlite(&second), 7)
        .unwrap();
    assert_eq!(switched(&mut engine, 7), Ok(connection));
    assert_eq!(tables(&mut engine, connection), ["only_second"]);
}

#[test]
fn an_open_transaction_blocks_the_switch_and_keeps_the_database() {
    let dir = tempfile::tempdir().unwrap();
    let first = database(dir.path(), "first.sqlite", "only_first");
    let second = database(dir.path(), "second.sqlite", "only_second");
    let mut engine = engine();
    let connection = engine.connect("sqlite", sqlite(&first)).unwrap();
    connected(&mut engine, connection);
    run(&mut engine, connection, "BEGIN");
    engine
        .switch_database(connection, sqlite(&second), 8)
        .unwrap();
    assert_eq!(
        switched(&mut engine, 8),
        Err("Commit or roll back the open transaction before you switch the database.".into())
    );
    run(&mut engine, connection, "ROLLBACK");
    assert_eq!(tables(&mut engine, connection), ["only_first"]);
}

#[test]
fn a_failed_switch_keeps_the_current_database() {
    let dir = tempfile::tempdir().unwrap();
    let first = database(dir.path(), "first.sqlite", "only_first");
    let mut engine = engine();
    let connection = engine.connect("sqlite", sqlite(&first)).unwrap();
    connected(&mut engine, connection);
    engine
        .switch_database(
            connection,
            sqlite(&dir.path().join("missing/none.sqlite")),
            9,
        )
        .unwrap();
    assert!(switched(&mut engine, 9).is_err());
    assert_eq!(tables(&mut engine, connection), ["only_first"]);
}

#[test]
fn a_saved_connection_switches_only_through_its_own_profile() {
    let dir = tempfile::tempdir().unwrap();
    let first = database(dir.path(), "first.sqlite", "only_first");
    let second = database(dir.path(), "second.sqlite", "only_second");
    let mut engine = engine();
    let connection = engine
        .connect_profile_with_secrets(profile("local", &first), no_secrets())
        .unwrap();
    connected(&mut engine, connection);
    assert_eq!(
        engine.switch_profile_database(connection, profile("other", &second), no_secrets(), 10),
        Err(SubmitError::InvalidInput)
    );
    assert_eq!(
        engine.switch_database(connection, sqlite(&second), 10),
        Err(SubmitError::InvalidInput)
    );
    engine
        .switch_profile_database(connection, profile("local", &second), no_secrets(), 11)
        .unwrap();
    assert_eq!(switched(&mut engine, 11), Ok(connection));
    assert_eq!(tables(&mut engine, connection), ["only_second"]);
}
