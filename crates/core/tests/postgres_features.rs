use choscordb_core::{
    ConnectionProfile, CsvOptions, CsvReadOptions, DatabaseActionKind, DatabaseActionRequest,
    Engine, Event, ImportEncoding, ImportFile, ImportFileFormat, ImportMapping, PostgresTls,
    ProfileConfiguration, ProfileSecrets, SafetyLevel, SqlFileMode, SqlFileRun,
    TableExportDestination, TableExportFormat, TableExportObject, TableExportRequest,
    TableImportRequest, TableImportTarget, TriggerScript, TriggerScriptRequest,
    plan_database_action, plan_table_export, trigger_script,
};
use choscordb_driver_api::{
    ConnectionId, ConnectionOptions, DatabaseDriver, EditBatch, GridCellExpression, GridEditColumn,
    GridEditDriver, GridEditRequest, GridEditRow, ImportConflict, MetadataAvailability, ObjectId,
    ObjectKind, PageSize, QueryOptions, SchemaObject, Secret, SetValueKind, SetValueOutcome,
    StructureChange, StructureChangeKind, StructureColumn, StructureTable, TlsMode, Value,
    grid_set_value, plan_grid_edits, plan_structure,
};
use choscordb_driver_postgres::PostgresDriver;
use choscordb_sql_language::statement_ranges;
use std::{
    collections::BTreeMap,
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

fn rows(engine: &mut Engine, connection: ConnectionId, sql: &str) -> Vec<Vec<Value>> {
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
    let event = until(
        engine,
        |event| matches!(event, Event::Schema { query: q, .. } | Event::QueryFailed { query: q, .. } if *q == query),
    );
    if let Event::QueryFailed { error, .. } = event {
        panic!("{sql}: {}", error.message);
    }
    engine.fetch_page_at(query, 0).unwrap();
    let Event::StoredPage { page, .. } = until(
        engine,
        |event| matches!(event, Event::StoredPage { query: q, .. } if *q == query),
    ) else {
        unreachable!()
    };
    engine.release_query(query).unwrap();
    page.rows.clone()
}

fn scalar(engine: &mut Engine, connection: ConnectionId, sql: &str) -> Value {
    rows(engine, connection, sql).remove(0).remove(0)
}

fn text(value: &str) -> Value {
    Value::Text(value.into())
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

fn table_object(engine: &mut Engine, connection: ConnectionId, name: &str) -> SchemaObject {
    let database = children(engine, connection, None).remove(0);
    let schema = child(engine, connection, Some(database.id), "public");
    let tables = child(engine, connection, Some(schema.id), "Tables");
    child(engine, connection, Some(tables.id), name)
}

fn structure_change(kind: StructureChangeKind, values: &[(&str, &str)]) -> StructureChange {
    StructureChange {
        kind,
        values: values
            .iter()
            .map(|(key, value)| ((*key).to_owned(), vec![(*value).to_owned()]))
            .collect::<BTreeMap<_, _>>(),
    }
}

fn structure_column(name: &str, data_type: &str, nullable: bool) -> StructureColumn {
    StructureColumn {
        name: name.into(),
        data_type: data_type.into(),
        nullable,
    }
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn structure_changes_add_change_index_reference_and_drop_columns() {
    let (mut engine, connection) = connect();
    for (token, sql) in [
        (41, "DROP TABLE IF EXISTS structure_live"),
        (42, "DROP TABLE IF EXISTS structure_live_parent"),
        (
            43,
            "CREATE TABLE structure_live_parent (id integer PRIMARY KEY)",
        ),
        (
            44,
            "CREATE TABLE structure_live (id integer PRIMARY KEY, name text, legacy text, parent_id integer)",
        ),
    ] {
        applied(&mut engine, connection, sql, token);
    }
    let table = StructureTable {
        qualified_name: "\"public\".\"structure_live\"".into(),
        new_table: false,
        columns: vec![
            structure_column("id", "integer", false),
            structure_column("name", "text", true),
            structure_column("legacy", "text", true),
            structure_column("parent_id", "integer", true),
        ],
        indexes: vec![],
        keys: vec![],
    };
    let statements = plan_structure(
        &table,
        PostgresDriver.structure_dialect().unwrap(),
        &[
            structure_change(
                StructureChangeKind::AddColumn,
                &[
                    ("name", "score"),
                    ("type", "integer"),
                    ("not_null", "true"),
                    ("default", "7"),
                    ("comment", "Live score"),
                ],
            ),
            structure_change(
                StructureChangeKind::ChangeColumn,
                &[
                    ("column", "name"),
                    ("name", "full_name"),
                    ("nullability", "not_null"),
                ],
            ),
            structure_change(
                StructureChangeKind::AddIndex,
                &[("name", "structure_live_score"), ("columns", "score")],
            ),
            structure_change(
                StructureChangeKind::AddForeignKey,
                &[
                    ("name", "structure_live_parent_fk"),
                    ("columns", "parent_id"),
                    ("ref_table", "public.structure_live_parent"),
                    ("ref_columns", "id"),
                    ("on_delete", "cascade"),
                ],
            ),
            structure_change(StructureChangeKind::DropColumn, &[("column", "legacy")]),
        ],
    )
    .unwrap();
    engine.apply_ddl(connection, statements, 45).unwrap();
    let event = until(&mut engine, |event| {
        matches!(
            event,
            Event::EditApplied {
                request_token: 45,
                ..
            } | Event::EditFailed {
                request_token: 45,
                ..
            }
        )
    });
    assert!(matches!(event, Event::EditApplied { .. }), "{event:?}");
    assert_eq!(
        rows(
            &mut engine,
            connection,
            "SELECT column_name::text, is_nullable::text, coalesce(column_default::text, '') \
             FROM information_schema.columns \
             WHERE table_schema = 'public' AND table_name = 'structure_live' \
             ORDER BY ordinal_position"
        ),
        [
            [text("id"), text("NO"), text("")],
            [text("full_name"), text("NO"), text("")],
            [text("parent_id"), text("YES"), text("")],
            [text("score"), text("NO"), text("7")],
        ]
    );
    assert_eq!(
        scalar(
            &mut engine,
            connection,
            "SELECT col_description(attrelid, attnum) FROM pg_attribute \
             WHERE attrelid = 'public.structure_live'::regclass AND attname = 'score'"
        ),
        text("Live score")
    );
    assert_eq!(
        scalar(
            &mut engine,
            connection,
            "SELECT indexdef FROM pg_indexes \
             WHERE schemaname = 'public' AND indexname = 'structure_live_score'"
        ),
        text("CREATE INDEX structure_live_score ON public.structure_live USING btree (score)")
    );
    assert_eq!(
        scalar(
            &mut engine,
            connection,
            "SELECT delete_rule::text FROM information_schema.referential_constraints \
             WHERE constraint_schema = 'public' AND constraint_name = 'structure_live_parent_fk'"
        ),
        text("CASCADE")
    );
    applied(&mut engine, connection, "DROP TABLE structure_live", 46);
    applied(
        &mut engine,
        connection,
        "DROP TABLE structure_live_parent",
        47,
    );
}

fn import(
    engine: &mut Engine,
    connection: ConnectionId,
    dir: &std::path::Path,
    body: &str,
    target: TableImportTarget,
    conflict: ImportConflict,
) -> Result<u64, (Option<u64>, String)> {
    let path = dir.join("rows.csv");
    std::fs::write(&path, body).unwrap();
    let request = TableImportRequest {
        driver: GridEditDriver::Postgres,
        file: ImportFile {
            path,
            format: ImportFileFormat::Csv(CsvReadOptions::default()),
            encoding: ImportEncoding::Utf8,
        },
        target,
        mapping: vec![
            ImportMapping {
                target: "id".into(),
                source: 0,
            },
            ImportMapping {
                target: "name".into(),
                source: 1,
            },
        ],
        conflict,
    };
    let import = engine.import_table(connection, request).unwrap();
    match until(engine, |event| {
        matches!(
            event,
            Event::TableImportFinished { import: id, .. }
                | Event::TableImportFailed { import: id, .. } if *id == import
        )
    }) {
        Event::TableImportFinished { rows, .. } => Ok(rows),
        Event::TableImportFailed { row, error, .. } => Err((row, error.message)),
        event => panic!("{event:?}"),
    }
}

fn people(engine: &mut Engine, connection: ConnectionId, table: &str) -> Vec<Vec<Value>> {
    rows(
        engine,
        connection,
        &format!("SELECT id::bigint, name FROM {table} ORDER BY 1"),
    )
}

fn person(id: i64, name: &str) -> Vec<Value> {
    vec![Value::Integer(id), text(name)]
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn a_csv_import_reports_the_failed_row_and_follows_the_conflict_policy() {
    let (mut engine, connection) = connect();
    let dir = tempfile::tempdir().unwrap();
    applied(
        &mut engine,
        connection,
        "DROP TABLE IF EXISTS import_live",
        51,
    );
    applied(
        &mut engine,
        connection,
        "CREATE TABLE import_live (id integer PRIMARY KEY, name text NOT NULL)",
        52,
    );
    applied(
        &mut engine,
        connection,
        "INSERT INTO import_live VALUES (1, 'Old')",
        53,
    );
    let existing = TableImportTarget::Existing {
        object: table_object(&mut engine, connection, "import_live").id,
    };
    let run = |engine: &mut Engine, body: &str, conflict| {
        import(
            engine,
            connection,
            dir.path(),
            body,
            existing.clone(),
            conflict,
        )
    };
    assert_eq!(
        run(&mut engine, "id,name\n2,Ann\n3,Bo\n", ImportConflict::Stop),
        Ok(2)
    );
    let (row, message) = run(
        &mut engine,
        "id,name\n4,Cy\n5,\n6,Di\n",
        ImportConflict::Stop,
    )
    .unwrap_err();
    assert_eq!(row, Some(2), "{message}");
    assert!(
        message.contains("violates not-null constraint"),
        "{message}"
    );
    assert_eq!(
        people(&mut engine, connection, "import_live"),
        [person(1, "Old"), person(2, "Ann"), person(3, "Bo")]
    );
    let (row, message) = run(&mut engine, "id,name\n1,New\n", ImportConflict::Stop).unwrap_err();
    assert_eq!(row, Some(1), "{message}");
    assert!(message.contains("duplicate key"), "{message}");
    assert_eq!(
        run(&mut engine, "id,name\n1,New\n5,Eve\n", ImportConflict::Skip),
        Ok(1)
    );
    assert_eq!(
        people(&mut engine, connection, "import_live"),
        [
            person(1, "Old"),
            person(2, "Ann"),
            person(3, "Bo"),
            person(5, "Eve")
        ]
    );
    assert_eq!(
        run(&mut engine, "id,name\n1,New\n", ImportConflict::Update),
        Ok(1)
    );
    assert_eq!(
        people(&mut engine, connection, "import_live")[0],
        person(1, "New")
    );
    let (row, message) = run(&mut engine, "id,name\n5,Fay\n", ImportConflict::Replace).unwrap_err();
    assert_eq!(row, None, "{message}");
    assert_eq!(
        message,
        "This connection does not support the conflict policy."
    );
    assert_eq!(
        people(&mut engine, connection, "import_live")[3],
        person(5, "Eve")
    );
    applied(&mut engine, connection, "DROP TABLE import_live", 54);
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn whole_tables_export_as_csv_files_and_as_a_sql_script_that_runs_again() {
    let (mut engine, connection) = connect();
    let dir = tempfile::tempdir().unwrap();
    for (token, sql) in [
        (61, "DROP TABLE IF EXISTS export_live_people"),
        (62, "DROP TABLE IF EXISTS export_live_notes"),
        (
            63,
            "CREATE TABLE export_live_people (id integer PRIMARY KEY, name text)",
        ),
        (
            64,
            "INSERT INTO export_live_people VALUES (1, 'Ann'), (2, NULL)",
        ),
        (65, "CREATE TABLE export_live_notes (body text)"),
        (66, "INSERT INTO export_live_notes VALUES ('a;b')"),
    ] {
        applied(&mut engine, connection, sql, token);
    }
    let objects: Vec<_> = ["export_live_people", "export_live_notes"]
        .into_iter()
        .map(|name| {
            let object = table_object(&mut engine, connection, name);
            TableExportObject {
                object_id: object.id.0,
                qualified_name: object.qualified_name,
                kind: "table".into(),
                relation_subtype: String::new(),
            }
        })
        .collect();
    let export = |engine: &mut Engine, format, destination| {
        let targets = plan_table_export(&TableExportRequest {
            driver: "postgres".into(),
            objects: objects.clone(),
            format,
            destination,
        })
        .unwrap();
        engine.export_tables(connection, targets).unwrap();
        until(engine, |event| {
            matches!(
                event,
                Event::TableExportFinished { .. } | Event::TableExportFailed { .. }
            )
        })
    };
    let event = export(
        &mut engine,
        TableExportFormat::Csv(CsvOptions {
            delimiter: ';',
            null_text: "NULL".into(),
            ..CsvOptions::default()
        }),
        TableExportDestination::Folder {
            path: dir.path().into(),
            pattern: "{schema}-{table}".into(),
        },
    );
    assert!(
        matches!(
            event,
            Event::TableExportFinished {
                tables: 2,
                rows: 3,
                ..
            }
        ),
        "{event:?}"
    );
    assert_eq!(
        std::fs::read_to_string(dir.path().join("public-export_live_people.csv")).unwrap(),
        "\"id\";\"name\"\r\n\"1\";\"Ann\"\r\n\"2\";NULL\r\n"
    );
    assert_eq!(
        std::fs::read_to_string(dir.path().join("public-export_live_notes.csv")).unwrap(),
        "\"body\"\r\n\"a;b\"\r\n"
    );
    let script = dir.path().join("all.sql");
    let event = export(
        &mut engine,
        TableExportFormat::Sql {
            structure: true,
            drop: true,
            data: true,
        },
        TableExportDestination::File(script.clone()),
    );
    assert!(
        matches!(
            event,
            Event::TableExportFinished {
                tables: 2,
                rows: 3,
                ..
            }
        ),
        "{event:?}"
    );
    assert_eq!(
        std::fs::read_to_string(&script).unwrap(),
        "DROP TABLE \"public\".\"export_live_people\";\n\
         CREATE TABLE \"public\".\"export_live_people\" (\n  \"id\" integer NOT NULL,\n  \"name\" text,\n  \
         CONSTRAINT \"export_live_people_pkey\" PRIMARY KEY (id)\n);\n\
         INSERT INTO \"public\".\"export_live_people\" (\"id\", \"name\") VALUES (1, E'Ann');\n\
         INSERT INTO \"public\".\"export_live_people\" (\"id\", \"name\") VALUES (2, NULL);\n\
         DROP TABLE \"public\".\"export_live_notes\";\n\
         CREATE TABLE \"public\".\"export_live_notes\" (\n  \"body\" text\n);\n\
         INSERT INTO \"public\".\"export_live_notes\" (\"body\") VALUES (E'a;b');\n"
    );
    applied(
        &mut engine,
        connection,
        "UPDATE export_live_people SET name = 'changed'",
        67,
    );
    engine
        .run_sql_file(
            connection,
            SqlFileRun {
                file: script,
                mysql: None,
                mode: SqlFileMode::StopOnError,
            },
        )
        .unwrap();
    let event = until(&mut engine, |event| {
        matches!(
            event,
            Event::SqlFileFinished { .. } | Event::SqlFileFailed { .. }
        )
    });
    assert!(
        matches!(event, Event::SqlFileFinished { failed: 0, .. }),
        "{event:?}"
    );
    assert_eq!(
        rows(
            &mut engine,
            connection,
            "SELECT id::bigint, name FROM export_live_people ORDER BY id"
        ),
        [
            vec![Value::Integer(1), text("Ann")],
            vec![Value::Integer(2), Value::Null]
        ]
    );
    applied(&mut engine, connection, "DROP TABLE export_live_people", 68);
    applied(&mut engine, connection, "DROP TABLE export_live_notes", 69);
}

fn options_for(database: &str) -> ConnectionOptions {
    let mut options = options();
    if let ConnectionOptions::Postgres { database: name, .. } = &mut options {
        *name = database.into();
    }
    options
}

fn switch(
    engine: &mut Engine,
    connection: ConnectionId,
    database: &str,
    token: u64,
) -> Result<(), String> {
    engine
        .switch_database(connection, options_for(database), token)
        .unwrap();
    match until(engine, |event| {
        matches!(event,
            Event::DatabaseSwitched { request_token, .. }
                | Event::DatabaseSwitchFailed { request_token, .. } if *request_token == token)
    }) {
        Event::DatabaseSwitched { connection: c, .. } => {
            assert_eq!(c, connection);
            Ok(())
        }
        Event::DatabaseSwitchFailed { error, .. } => Err(error.message),
        event => panic!("{event:?}"),
    }
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn a_connection_switches_the_database_in_place() {
    let (mut engine, connection) = connect();
    let home = std::env::var("CHOSCORDB_TEST_POSTGRES_DATABASE").unwrap();
    applied(
        &mut engine,
        connection,
        "DROP DATABASE IF EXISTS switch_live_target",
        71,
    );
    applied(
        &mut engine,
        connection,
        "CREATE DATABASE switch_live_target",
        72,
    );
    assert_eq!(
        switch(&mut engine, connection, "switch_live_target", 73),
        Ok(())
    );
    assert_eq!(
        scalar(&mut engine, connection, "SELECT current_database()::text"),
        text("switch_live_target")
    );
    assert!(
        switch(&mut engine, connection, "switch_live_missing", 74).is_err(),
        "a missing database switched"
    );
    assert_eq!(
        scalar(&mut engine, connection, "SELECT current_database()::text"),
        text("switch_live_target")
    );
    assert_eq!(switch(&mut engine, connection, &home, 75), Ok(()));
    assert_eq!(
        scalar(&mut engine, connection, "SELECT current_database()::text"),
        text(&home)
    );
    applied(
        &mut engine,
        connection,
        "DROP DATABASE switch_live_target",
        76,
    );
}

fn read_only_profile() -> ConnectionProfile {
    let get =
        |name| std::env::var(name).expect("repository PostgreSQL fixture environment required");
    ConnectionProfile {
        id: "read-only-live".into(),
        name: "Read-only live".into(),
        group_id: None,
        configuration: ProfileConfiguration::Postgres {
            proxy: None,
            host: get("CHOSCORDB_TEST_POSTGRES_HOST"),
            port: get("CHOSCORDB_TEST_POSTGRES_PORT").parse().unwrap(),
            database: get("CHOSCORDB_TEST_POSTGRES_DATABASE"),
            user: get("CHOSCORDB_TEST_POSTGRES_USER"),
            tls: PostgresTls {
                mode: TlsMode::VerifyFull,
                root_certificate_path: Some(get("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE")),
                client_identity_path: None,
            },
            ssh: None,
        },
        credential_ref: None,
        proxy_credential_ref: None,
        ssh_jump_credential_refs: Default::default(),
        ssh_jump_private_key_refs: Default::default(),
        environment: Default::default(),
        environment_label: None,
        color: Default::default(),
        safety: SafetyLevel::ReadOnly,
        tls_credential_ref: None,
        ssh_credential_ref: None,
        ssh_private_key_ref: None,
        row_limit: 0,
    }
}

fn statement_error(engine: &mut Engine, connection: ConnectionId, sql: &str) -> String {
    let query = engine
        .execute(connection, sql.into(), QueryOptions::default())
        .unwrap();
    engine.fetch_page_at(query, 0).unwrap();
    match until(engine, |event| {
        matches!(event,
            Event::QueryFailed { query: q, .. } | Event::QueryFinished { query: q, .. } if *q == query)
    }) {
        Event::QueryFailed { error, .. } => error.message,
        event => panic!("{sql} ran on a read-only session: {event:?}"),
    }
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn a_read_only_profile_rejects_writes_in_the_database_session() {
    let (mut engine, connection) = connect();
    applied(
        &mut engine,
        connection,
        "DROP TABLE IF EXISTS read_only_live",
        81,
    );
    applied(
        &mut engine,
        connection,
        "CREATE TABLE read_only_live (id integer)",
        82,
    );
    let locked = engine
        .connect_profile(
            read_only_profile(),
            Some(Secret::new(
                std::env::var("CHOSCORDB_TEST_POSTGRES_PASSWORD").unwrap(),
            )),
        )
        .unwrap();
    until(
        &mut engine,
        |event| matches!(event, Event::Connected { connection: c, .. } if *c == locked),
    );
    assert_eq!(
        statement_error(&mut engine, locked, "INSERT INTO read_only_live VALUES (1)"),
        "cannot execute INSERT in a read-only transaction"
    );
    assert_eq!(
        statement_error(
            &mut engine,
            locked,
            "/* plain */ WITH moved AS (DELETE FROM read_only_live RETURNING id) SELECT count(*) FROM moved"
        ),
        "cannot execute SELECT in a read-only transaction"
    );
    assert_eq!(
        statement_error(
            &mut engine,
            locked,
            "CREATE TABLE read_only_other (id integer)"
        ),
        "cannot execute CREATE TABLE in a read-only transaction"
    );
    engine
        .apply_ddl(locked, vec!["DROP TABLE read_only_live".into()], 83)
        .unwrap();
    let event = until(&mut engine, |event| {
        matches!(
            event,
            Event::EditApplied {
                request_token: 83,
                ..
            } | Event::EditFailed {
                request_token: 83,
                ..
            }
        )
    });
    assert!(
        matches!(&event, Event::EditFailed { error, .. }
            if error.message.contains("cannot execute DROP TABLE in a read-only transaction")),
        "{event:?}"
    );
    assert_eq!(
        scalar(&mut engine, locked, "SELECT count(*) FROM read_only_live"),
        Value::Integer(0)
    );
    engine
        .switch_profile_database(
            locked,
            read_only_profile(),
            ProfileSecrets {
                database: Some(Secret::new(
                    std::env::var("CHOSCORDB_TEST_POSTGRES_PASSWORD").unwrap(),
                )),
                ssh: None,
                ssh_private_key: None,
                tls: None,
                proxy: None,
                ssh_jumps: Default::default(),
                ssh_jump_private_keys: Default::default(),
            },
            85,
        )
        .unwrap();
    let event = until(&mut engine, |event| {
        matches!(
            event,
            Event::DatabaseSwitched {
                request_token: 85,
                ..
            } | Event::DatabaseSwitchFailed {
                request_token: 85,
                ..
            }
        )
    });
    assert!(matches!(event, Event::DatabaseSwitched { .. }), "{event:?}");
    assert_eq!(
        statement_error(&mut engine, locked, "INSERT INTO read_only_live VALUES (2)"),
        "cannot execute INSERT in a read-only transaction"
    );
    applied(&mut engine, connection, "DROP TABLE read_only_live", 84);
}

#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn set_value_writes_default_and_now_into_updated_and_inserted_rows() {
    let (mut engine, connection) = connect();
    applied(
        &mut engine,
        connection,
        "DROP TABLE IF EXISTS set_value_live",
        91,
    );
    applied(
        &mut engine,
        connection,
        "CREATE TABLE set_value_live (id integer PRIMARY KEY, label text DEFAULT 'fallback', \
         stamp timestamp, day date)",
        92,
    );
    applied(
        &mut engine,
        connection,
        "INSERT INTO set_value_live VALUES (7, 'typed', NULL, NULL)",
        93,
    );
    let expression = |kind, database_type, inserted| match grid_set_value(
        kind,
        GridEditDriver::Postgres,
        database_type,
        inserted,
        None,
    ) {
        Ok(SetValueOutcome::Expression(expression)) => Some(expression),
        Ok(SetValueOutcome::Omit) => Some(GridCellExpression::Default),
        other => panic!("{other:?}"),
    };
    let column = |name: &str, database_type: &str, key| GridEditColumn {
        name: name.into(),
        result_name: name.into(),
        database_type: database_type.into(),
        key,
        generated: false,
        enum_source_column: String::new(),
        enum_choices: vec![],
    };
    let plan = plan_grid_edits(&GridEditRequest {
        driver: GridEditDriver::Postgres,
        qualified_name: "\"public\".\"set_value_live\"".into(),
        parameter_style: "$".into(),
        reason: String::new(),
        object_read_only: false,
        columns: vec![
            column("id", "integer", true),
            column("label", "text", false),
            column("stamp", "timestamp without time zone", false),
            column("day", "date", false),
        ],
        rows: vec![
            GridEditRow {
                current: vec![Value::Integer(7), text("typed"), Value::Null, Value::Null],
                original: vec![Value::Integer(7), text("typed"), Value::Null, Value::Null],
                touched: vec![false, true, true, true],
                expressions: vec![
                    None,
                    expression(SetValueKind::Default, "text", false),
                    expression(SetValueKind::Now, "timestamp without time zone", false),
                    expression(SetValueKind::Now, "date", false),
                ],
                inserted: false,
                deleted: false,
            },
            GridEditRow {
                current: vec![Value::Integer(8), Value::Null, Value::Null, Value::Null],
                original: vec![],
                touched: vec![true, true, true, false],
                expressions: vec![
                    None,
                    expression(SetValueKind::Default, "text", true),
                    expression(SetValueKind::Now, "timestamp without time zone", true),
                    None,
                ],
                inserted: true,
                deleted: false,
            },
        ],
    })
    .unwrap();
    engine
        .apply_edit_batch(
            connection,
            EditBatch {
                statements: plan
                    .statements
                    .into_iter()
                    .map(|planned| planned.statement)
                    .collect(),
            },
            None,
            94,
        )
        .unwrap();
    let event = until(&mut engine, |event| {
        matches!(
            event,
            Event::EditApplied {
                request_token: 94,
                ..
            } | Event::EditFailed {
                request_token: 94,
                ..
            }
        )
    });
    assert!(matches!(event, Event::EditApplied { .. }), "{event:?}");
    assert_eq!(
        rows(
            &mut engine,
            connection,
            "SELECT id::bigint, label, stamp IS NOT NULL, day = stamp::date FROM set_value_live ORDER BY id"
        ),
        [
            vec![
                Value::Integer(7),
                text("fallback"),
                Value::Bool(true),
                Value::Bool(true)
            ],
            vec![
                Value::Integer(8),
                text("fallback"),
                Value::Bool(true),
                Value::Null
            ],
        ]
    );
    applied(&mut engine, connection, "DROP TABLE set_value_live", 95);
}
