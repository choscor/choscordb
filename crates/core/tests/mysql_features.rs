use choscordb_core::{
    DatabaseActionKind, DatabaseActionRequest, Engine, Event, SqlFileMode, SqlFileRun,
    plan_database_action,
};
use choscordb_driver_api::{
    ConnectionId, ErrorKind, GridEditDriver, MetadataAvailability, ObjectId, PageSize,
    QueryOptions, TlsMode, Value,
};
use choscordb_sql_language::MysqlSqlMode;
#[path = "../../driver-mysql/tests/support/server.rs"]
mod server;
use std::time::{Duration, Instant};

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
            "timed out waiting for a MySQL engine event"
        );
        std::thread::sleep(Duration::from_millis(1));
    }
}

fn connect() -> (Engine, ConnectionId) {
    let mut engine = Engine::new(Default::default(), vec![server::driver()]).unwrap();
    let connection = engine
        .connect(server::driver().id(), server::options(TlsMode::Disable))
        .unwrap();
    until(&mut engine, |event| {
        matches!(
            event,
            Event::Connected { .. } | Event::ConnectionFailed { .. }
        )
    });
    (engine, connection)
}

fn driver_name() -> &'static str {
    if server::mariadb() {
        "mariadb"
    } else {
        "mysql"
    }
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

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn explain_returns_the_plan_tree_of_a_statement() {
    let (mut engine, connection) = connect();
    engine
        .explain(
            connection,
            GridEditDriver::Mysql,
            "SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA = 'mysql'",
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
    assert!(!nodes.is_empty());
    assert_eq!(nodes[0].depth, 0);
    assert!(!nodes[0].operation.is_empty(), "{nodes:?}");
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn a_sql_file_stops_at_the_failed_line_or_rolls_back_the_transaction() {
    let (mut engine, connection) = connect();
    let dir = tempfile::tempdir().unwrap();
    let file = dir.path().join("script.sql");
    std::fs::write(
        &file,
        "DROP TABLE IF EXISTS sql_file_rows;\n\
         CREATE TABLE sql_file_rows (id INT PRIMARY KEY) ENGINE=InnoDB;\n\
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
                    mysql: Some(MysqlSqlMode {
                        mariadb: server::mariadb(),
                        ..MysqlSqlMode::default()
                    }),
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
            "SELECT COUNT(*) FROM sql_file_rows"
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
            "SELECT COUNT(*) FROM sql_file_rows"
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
            "SELECT COUNT(*) FROM sql_file_rows"
        ),
        Value::Integer(2)
    );
    let dropped = apply(
        &mut engine,
        connection,
        "DROP TABLE sql_file_rows".into(),
        9,
    );
    assert!(matches!(dropped, Event::EditApplied { .. }), "{dropped:?}");
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn database_actions_create_and_drop_a_database_and_reject_a_rename() {
    let (mut engine, connection) = connect();
    let request = |kind, name: &str| DatabaseActionRequest {
        driver: driver_name().into(),
        kind,
        current: "choscordb_test".into(),
        name: name.into(),
        new_name: "renamed_database".into(),
        encoding: "utf8mb4".into(),
        collation: "utf8mb4_unicode_ci".into(),
        confirmation: name.into(),
    };
    let create =
        plan_database_action(&request(DatabaseActionKind::Create, "action_target")).unwrap();
    let event = apply(&mut engine, connection, create, 11);
    assert!(matches!(event, Event::EditApplied { .. }), "{event:?}");
    engine.list_databases(connection, 12).unwrap();
    let Event::Databases { databases, .. } = until(&mut engine, |event| {
        matches!(
            event,
            Event::Databases {
                request_token: 12,
                ..
            } | Event::DatabasesFailed {
                request_token: 12,
                ..
            }
        )
    }) else {
        panic!("the database list failed")
    };
    assert!(
        databases
            .choices
            .iter()
            .any(|choice| choice.label == "action_target")
    );
    assert_eq!(
        scalar(
            &mut engine,
            connection,
            "SELECT DEFAULT_COLLATION_NAME FROM information_schema.SCHEMATA WHERE SCHEMA_NAME = 'action_target'"
        ),
        Value::Text("utf8mb4_unicode_ci".into())
    );
    let rename =
        plan_database_action(&request(DatabaseActionKind::Rename, "action_target")).unwrap_err();
    assert_eq!(rename.kind, ErrorKind::Unsupported);
    let drop = plan_database_action(&request(DatabaseActionKind::Drop, "action_target")).unwrap();
    let event = apply(&mut engine, connection, drop, 13);
    assert!(matches!(event, Event::EditApplied { .. }), "{event:?}");
    assert_eq!(
        scalar(
            &mut engine,
            connection,
            "SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME = 'action_target'"
        ),
        Value::Integer(0)
    );
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn the_whole_schema_graph_shows_each_table_and_its_foreign_keys() {
    let (mut engine, connection) = connect();
    for (token, sql) in [
        (21, "DROP TABLE IF EXISTS graph_whole_child"),
        (22, "DROP TABLE IF EXISTS graph_whole_parent"),
        (
            23,
            "CREATE TABLE graph_whole_parent (id INT PRIMARY KEY) ENGINE=InnoDB",
        ),
        (
            24,
            "CREATE TABLE graph_whole_child (id INT PRIMARY KEY, parent_id INT, \
             CONSTRAINT graph_whole_fk FOREIGN KEY (parent_id) REFERENCES graph_whole_parent (id)) ENGINE=InnoDB",
        ),
    ] {
        let event = apply(&mut engine, connection, sql.into(), token);
        assert!(matches!(event, Event::EditApplied { .. }), "{event:?}");
    }
    engine
        .load_object_graph_request(connection, ObjectId(r#"["choscordb_test"]"#.into()), 25)
        .unwrap();
    let event = until(&mut engine, |event| {
        matches!(
            event,
            Event::ObjectGraph {
                request_token: 25,
                ..
            } | Event::ObjectGraphFailed {
                request_token: 25,
                ..
            }
        )
    });
    let Event::ObjectGraph { graph, .. } = event else {
        panic!("{event:?}")
    };
    assert_eq!(graph.availability, MetadataAvailability::Available);
    let names: Vec<_> = graph
        .tables
        .iter()
        .map(|table| table.qualified_name.as_str())
        .collect();
    assert!(
        names.contains(&"`choscordb_test`.`graph_whole_parent`"),
        "{names:?}"
    );
    assert!(
        names.contains(&"`choscordb_test`.`graph_whole_child`"),
        "{names:?}"
    );
    assert!(
        graph
            .edges
            .iter()
            .any(|edge| edge.source_columns == ["parent_id"] && edge.target_columns == ["id"]),
        "{:?}",
        graph.edges
    );
    for (token, sql) in [
        (26, "DROP TABLE graph_whole_child"),
        (27, "DROP TABLE graph_whole_parent"),
    ] {
        let event = apply(&mut engine, connection, sql.into(), token);
        assert!(matches!(event, Event::EditApplied { .. }), "{event:?}");
    }
}
