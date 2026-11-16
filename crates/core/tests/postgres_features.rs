use choscordb_core::{
    DatabaseActionKind, DatabaseActionRequest, Engine, Event, SqlFileMode, SqlFileRun,
    TriggerScript, TriggerScriptRequest, plan_database_action, trigger_script,
};
use choscordb_driver_api::{
    ConnectionId, ConnectionOptions, GridEditDriver, MetadataAvailability, ObjectId, ObjectKind,
    PageSize, QueryOptions, SchemaObject, Secret, TlsMode, Value,
};
use choscordb_driver_postgres::PostgresDriver;
use choscordb_sql_language::statement_ranges;
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
    let deadline = Instant::now() + Duration::from_secs(20);
    loop {
        if let Some(event) = engine.try_event()
            && predicate(&event)
        {
            return event;
        }
        assert!(
            Instant::now() < deadline,
            "timed out waiting for a PostgreSQL engine event"
        );
        std::thread::sleep(Duration::from_millis(1));
    }
}

fn connect() -> (Engine, ConnectionId) {
    let mut engine = Engine::new(Default::default(), vec![Arc::new(PostgresDriver)]).unwrap();
    let connection = engine.connect("postgres", options()).unwrap();
    until(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    (engine, connection)
}

fn scalar(engine: &mut Engine, connection: ConnectionId, sql: &str) -> Value {
    let query = engine
        .execute(
            connection,
            sql.into(),
            QueryOptions {
                page_size: PageSize::new(100).unwrap(),
                ..QueryOptions::default()
            },
        )
        .unwrap();
    until(
        engine,
        |event| matches!(event, Event::Schema { query: q, .. } | Event::QueryFailed { query: q, .. } if *q == query),
    );
    engine.fetch_page_at(query, 0).unwrap();
    let Event::StoredPage { page, .. } = until(
        engine,
        |event| matches!(event, Event::StoredPage { query: q, .. } if *q == query),
    ) else {
        unreachable!()
    };
    engine.release_query(query).unwrap();
    page.rows[0][0].clone()
}

fn apply(engine: &mut Engine, connection: ConnectionId, sql: String, token: u64) -> Event {
    engine.apply_ddl(connection, vec![sql], token).unwrap();
    until(engine, |event| {
        matches!(event,
            Event::EditApplied { request_token, .. } | Event::EditFailed { request_token, .. }
                if *request_token == token)
    })
}

fn applied(engine: &mut Engine, connection: ConnectionId, sql: &str, token: u64) {
    match apply(engine, connection, sql.into(), token) {
        Event::EditApplied { .. } => {}
        Event::EditFailed { error, .. } => panic!("{sql}: {}", error.message),
        event => panic!("{sql}: {event:?}"),
    }
}

fn children(
    engine: &mut Engine,
    connection: ConnectionId,
    parent: Option<ObjectId>,
) -> Vec<SchemaObject> {
    engine.load_metadata(connection, parent.clone()).unwrap();
    let Event::Metadata { objects, .. } = until(
        engine,
        |event| matches!(event, Event::Metadata { parent: p, .. } if *p == parent),
    ) else {
        unreachable!()
    };
    objects
}

fn child(
    engine: &mut Engine,
    connection: ConnectionId,
    parent: Option<ObjectId>,
    name: &str,
) -> SchemaObject {
    children(engine, connection, parent)
        .into_iter()
        .find(|object| object.name == name)
        .unwrap_or_else(|| panic!("{name} is not in the navigator"))
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn explain_returns_the_plan_tree_with_cost_and_rows() {
    let (mut engine, connection) = connect();
    engine
        .explain(
            connection,
            GridEditDriver::Postgres,
            "SELECT relname FROM pg_class ORDER BY reltuples",
            7,
        )
        .unwrap();
    let event = until(&mut engine, |event| {
        matches!(
            event,
            Event::ExplainReady { .. } | Event::ExplainFailed { .. }
        )
    });
    let Event::ExplainReady {
        request_token: 7,
        nodes,
        ..
    } = event
    else {
        panic!("{event:?}")
    };
    assert_eq!(nodes[0].depth, 0);
    assert_eq!(nodes[0].operation, "Sort");
    assert!(
        !nodes[0].cost.is_empty() && !nodes[0].rows.is_empty(),
        "{nodes:?}"
    );
    assert!(nodes.iter().any(|node| node.depth == 1), "{nodes:?}");
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn a_sql_file_stops_at_the_failed_line_or_rolls_back_the_transaction() {
    let (mut engine, connection) = connect();
    let dir = tempfile::tempdir().unwrap();
    let file = dir.path().join("script.sql");
    std::fs::write(
        &file,
        "DROP TABLE IF EXISTS sql_file_rows;\n\
         CREATE TABLE sql_file_rows (id integer PRIMARY KEY);\n\
         INSERT INTO sql_file_rows VALUES (1);\n\
         INSERT INTO sql_file_rows VALUES (1);\n\
         INSERT INTO sql_file_rows VALUES (2);\n",
    )
    .unwrap();
    let run = |engine: &mut Engine, mode| {
        engine
            .run_sql_file(
                connection,
                SqlFileRun {
                    file: file.clone(),
                    mysql: None,
                    mode,
                },
            )
            .unwrap();
        until(engine, |event| {
            matches!(
                event,
                Event::SqlFileFinished { .. } | Event::SqlFileFailed { .. }
            )
        })
    };
    let event = run(&mut engine, SqlFileMode::StopOnError);
    assert!(
        matches!(&event, Event::SqlFileFailed { statements: 3, error, .. }
            if error.message.starts_with("Line 4: ")),
        "{event:?}"
    );
    assert_eq!(
        scalar(
            &mut engine,
            connection,
            "SELECT count(*) FROM sql_file_rows"
        ),
        Value::Integer(1)
    );
    std::fs::write(
        &file,
        "INSERT INTO sql_file_rows VALUES (5);\nINSERT INTO sql_file_rows VALUES (1);\n",
    )
    .unwrap();
    let event = run(&mut engine, SqlFileMode::Transaction);
    assert!(
        matches!(&event, Event::SqlFileFailed { statements: 1, error, .. }
            if error.message.starts_with("Line 2: ")
                && error.message.ends_with("The transaction was rolled back.")),
        "{event:?}"
    );
    assert_eq!(
        scalar(
            &mut engine,
            connection,
            "SELECT count(*) FROM sql_file_rows"
        ),
        Value::Integer(1)
    );
    let event = run(&mut engine, SqlFileMode::ContinueOnError);
    let Event::SqlFileFinished {
        statements: 1,
        failed: 1,
        ..
    } = event
    else {
        panic!("{event:?}")
    };
    assert_eq!(
        scalar(
            &mut engine,
            connection,
            "SELECT count(*) FROM sql_file_rows"
        ),
        Value::Integer(2)
    );
    applied(&mut engine, connection, "DROP TABLE sql_file_rows", 9);
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn database_actions_create_rename_and_drop_a_database() {
    let (mut engine, connection) = connect();
    let current = std::env::var("CHOSCORDB_TEST_POSTGRES_DATABASE").unwrap();
    let request = |kind, name: &str| DatabaseActionRequest {
        driver: "postgres".into(),
        kind,
        current: current.clone(),
        name: name.into(),
        new_name: "action renamed".into(),
        encoding: "UTF8".into(),
        collation: "C".into(),
        confirmation: name.into(),
    };
    let create =
        plan_database_action(&request(DatabaseActionKind::Create, "action target")).unwrap();
    let event = apply(&mut engine, connection, create, 11);
    assert!(matches!(event, Event::EditApplied { .. }), "{event:?}");
    assert_eq!(
        scalar(
            &mut engine,
            connection,
            "SELECT datcollate FROM pg_database WHERE datname = 'action target'"
        ),
        Value::Text("C".into())
    );
    let rename =
        plan_database_action(&request(DatabaseActionKind::Rename, "action target")).unwrap();
    let event = apply(&mut engine, connection, rename, 12);
    assert!(matches!(event, Event::EditApplied { .. }), "{event:?}");
    engine.list_databases(connection, 13).unwrap();
    let Event::Databases { databases, .. } = until(&mut engine, |event| {
        matches!(
            event,
            Event::Databases {
                request_token: 13,
                ..
            } | Event::DatabasesFailed {
                request_token: 13,
                ..
            }
        )
    }) else {
        panic!("the database list failed")
    };
    let labels: Vec<_> = databases
        .choices
        .iter()
        .map(|choice| choice.label.as_str())
        .collect();
    assert!(labels.contains(&"action renamed"), "{labels:?}");
    assert!(!labels.contains(&"action target"), "{labels:?}");
    let drop = plan_database_action(&request(DatabaseActionKind::Drop, "action renamed")).unwrap();
    let event = apply(&mut engine, connection, drop, 14);
    assert!(matches!(event, Event::EditApplied { .. }), "{event:?}");
    assert_eq!(
        scalar(
            &mut engine,
            connection,
            "SELECT count(*) FROM pg_database WHERE datname = 'action renamed'"
        ),
        Value::Integer(0)
    );
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn trigger_scripts_create_list_and_drop_a_trigger() {
    let (mut engine, connection) = connect();
    applied(
        &mut engine,
        connection,
        "DROP TABLE IF EXISTS trigger_rows",
        21,
    );
    applied(
        &mut engine,
        connection,
        "CREATE TABLE trigger_rows (id integer PRIMARY KEY)",
        22,
    );
    let database = children(&mut engine, connection, None).remove(0);
    let schema = child(&mut engine, connection, Some(database.id), "public");
    let tables = child(&mut engine, connection, Some(schema.id), "Tables");
    let table = child(&mut engine, connection, Some(tables.id), "trigger_rows");
    let request = |trigger: &str| TriggerScriptRequest {
        driver: "postgres".into(),
        table_id: table.id.0.clone(),
        table_name: table.qualified_name.clone(),
        trigger: trigger.into(),
        definition: String::new(),
    };
    let create = trigger_script(TriggerScript::Create, &request("")).unwrap();
    let statements: Vec<_> = statement_ranges(&create)
        .into_iter()
        .map(|range| create[range].trim().to_owned())
        .collect();
    assert_eq!(statements.len(), 2, "{statements:?}");
    for (index, statement) in statements.iter().enumerate() {
        applied(&mut engine, connection, statement, 230 + index as u64);
    }
    let triggers: Vec<_> = children(&mut engine, connection, Some(table.id.clone()))
        .into_iter()
        .filter(|object| object.kind == ObjectKind::Trigger)
        .map(|object| object.name)
        .collect();
    assert_eq!(triggers, ["trigger_rows_trigger"]);
    let drop = trigger_script(TriggerScript::Drop, &request("trigger_rows_trigger")).unwrap();
    applied(&mut engine, connection, &drop, 24);
    assert!(
        children(&mut engine, connection, Some(table.id.clone()))
            .iter()
            .all(|object| object.kind != ObjectKind::Trigger)
    );
    applied(&mut engine, connection, "DROP TABLE trigger_rows", 25);
    applied(
        &mut engine,
        connection,
        "DROP FUNCTION public.trigger_rows_trigger()",
        26,
    );
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn the_whole_schema_graph_shows_each_table_and_its_foreign_keys() {
    let (mut engine, connection) = connect();
    applied(
        &mut engine,
        connection,
        "DROP SCHEMA IF EXISTS graph_whole CASCADE",
        31,
    );
    applied(&mut engine, connection, "CREATE SCHEMA graph_whole", 32);
    applied(
        &mut engine,
        connection,
        "CREATE TABLE graph_whole.parent (id integer PRIMARY KEY)",
        33,
    );
    applied(
        &mut engine,
        connection,
        "CREATE TABLE graph_whole.child (id integer PRIMARY KEY, parent_id integer REFERENCES graph_whole.parent (id))",
        34,
    );
    let database = children(&mut engine, connection, None).remove(0);
    let schema = child(&mut engine, connection, Some(database.id), "graph_whole");
    engine
        .load_object_graph_request(connection, schema.id, 35)
        .unwrap();
    let event = until(&mut engine, |event| {
        matches!(
            event,
            Event::ObjectGraph {
                request_token: 35,
                ..
            } | Event::ObjectGraphFailed {
                request_token: 35,
                ..
            }
        )
    });
    let Event::ObjectGraph { graph, .. } = event else {
        panic!("{event:?}")
    };
    assert_eq!(graph.availability, MetadataAvailability::Available);
    let mut names: Vec<_> = graph
        .tables
        .iter()
        .map(|table| table.qualified_name.as_str())
        .collect();
    names.sort_unstable();
    assert_eq!(
        names,
        ["\"graph_whole\".\"child\"", "\"graph_whole\".\"parent\""]
    );
    assert!(
        graph
            .edges
            .iter()
            .any(|edge| edge.source_columns == ["parent_id"] && edge.target_columns == ["id"]),
        "{:?}",
        graph.edges
    );
    applied(
        &mut engine,
        connection,
        "DROP SCHEMA graph_whole CASCADE",
        36,
    );
}
