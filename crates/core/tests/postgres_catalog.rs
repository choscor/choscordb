//! Catalog reads beside the query session. Run against the disposable PostgreSQL fixture.
use choscordb_core::{Engine, Event, QueryState};
use choscordb_driver_api::{
    ConnectionId, ConnectionOptions, ObjectId, ObjectKind, QueryId, QueryOptions, SchemaObject,
    Secret, TlsMode,
};
use choscordb_driver_postgres::PostgresDriver;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};

fn options() -> ConnectionOptions {
    let get =
        |name| std::env::var(name).expect("repository PostgreSQL fixture environment required");
    ConnectionOptions::Postgres {
        proxy: None,
        proxy_secret: None,
        ssh: None,
        host: get("CHOSCORDB_TEST_POSTGRES_HOST"),
        port: get("CHOSCORDB_TEST_POSTGRES_PORT").parse().unwrap(),
        database: get("CHOSCORDB_TEST_POSTGRES_DATABASE"),
        user: get("CHOSCORDB_TEST_POSTGRES_USER"),
        password: Some(Secret::new(get("CHOSCORDB_TEST_POSTGRES_PASSWORD"))),
        ssh_secret: None,
        ssh_private_key: None,
        ssh_jump_secrets: Default::default(),
        ssh_jump_private_keys: Default::default(),
        tls: TlsMode::VerifyFull,
        tls_identity: None,
        root_certificate: Some(get("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE").into()),
    }
}

fn until(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) -> Event {
    let deadline = Instant::now() + Duration::from_secs(15);
    loop {
        if let Some(event) = engine.try_event() {
            if predicate(&event) {
                return event;
            }
            assert!(
                !matches!(
                    event,
                    Event::ConnectionFailed { .. }
                        | Event::MetadataFailed { .. }
                        | Event::QueryFailed { .. }
                ),
                "unexpected failure"
            );
        }
        assert!(
            Instant::now() < deadline,
            "timed out waiting for engine event"
        );
        std::thread::sleep(Duration::from_millis(1));
    }
}

fn connect(engine: &mut Engine) -> ConnectionId {
    let connection = engine.connect("postgres", options()).unwrap();
    until(engine, |event| matches!(event, Event::Connected { .. }));
    connection
}

fn children(
    engine: &mut Engine,
    connection: ConnectionId,
    parent: Option<ObjectId>,
) -> Vec<SchemaObject> {
    engine.load_metadata(connection, parent.clone()).unwrap();
    match until(
        engine,
        |event| matches!(event, Event::Metadata { parent: p, .. } if *p == parent),
    ) {
        Event::Metadata { objects, .. } => objects,
        _ => unreachable!(),
    }
}

/// Whether the navigator shows a table of this name under `public` and then `Tables`.
fn shows_table(engine: &mut Engine, connection: ConnectionId, name: &str) -> bool {
    let child = |engine: &mut Engine, parent, name: &str| {
        children(engine, connection, parent)
            .into_iter()
            .find(|object| object.name == name)
    };
    let database = children(engine, connection, None).remove(0);
    let schema = child(engine, Some(database.id), "public").unwrap();
    let tables = child(engine, Some(schema.id), "Tables").unwrap();
    child(engine, Some(tables.id), name).is_some_and(|table| table.kind == ObjectKind::Table)
}

/// Reads the result to its end, as the desktop does after the schema arrives.
fn finished(engine: &mut Engine, query: QueryId) {
    until(
        engine,
        |event| matches!(event, Event::Schema { query: id, .. } if *id == query),
    );
    engine.fetch_page(query).unwrap();
    until(
        engine,
        |event| matches!(event, Event::QueryFinished { query: id, .. } if *id == query),
    );
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn a_navigator_expand_does_not_wait_for_a_running_query() {
    let mut engine = Engine::new(Default::default(), vec![Arc::new(PostgresDriver)]).unwrap();
    let connection = connect(&mut engine);
    let query = engine
        .execute(
            connection,
            "SELECT pg_sleep(5)".into(),
            QueryOptions::default(),
        )
        .unwrap();
    until(
        &mut engine,
        |event| matches!(event, Event::QueryState { query: id, state: QueryState::Running } if *id == query),
    );
    let started = Instant::now();
    let roots = children(&mut engine, connection, None);
    let elapsed = started.elapsed();
    println!("navigator expand during a query: {elapsed:?}");
    assert!(!roots.is_empty());
    assert!(elapsed < Duration::from_secs(3), "{elapsed:?}");
    finished(&mut engine, query);
    engine.release_query(query).unwrap();
    engine.disconnect(connection).unwrap();
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn the_navigator_shows_uncommitted_ddl_of_the_open_transaction() {
    let table = format!("choscordb_catalog_{}", uuid::Uuid::new_v4().simple());
    let mut engine = Engine::new(Default::default(), vec![Arc::new(PostgresDriver)]).unwrap();
    let connection = connect(&mut engine);
    // A first expand opens the catalog session before the transaction starts.
    assert!(!shows_table(&mut engine, connection, &table));
    let query = engine
        .execute(
            connection,
            format!("CREATE TABLE {table} (id integer)"),
            QueryOptions {
                auto_commit: false,
                ..Default::default()
            },
        )
        .unwrap();
    finished(&mut engine, query);
    assert!(shows_table(&mut engine, connection, &table));
    // The disconnect rolls the transaction back, so the table is not left behind.
    engine.disconnect(connection).unwrap();
}
