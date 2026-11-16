use choscordb_core::{
    ConnectionProfile, CsvOptions, CsvReadOptions, DatabaseActionKind, DatabaseActionRequest,
    Engine, Event, ImportEncoding, ImportFile, ImportFileFormat, ImportMapping, PostgresTls,
    ProfileConfiguration, ProfileSecrets, SafetyLevel, SqlFileMode, SqlFileRun,
    TableExportDestination, TableExportFormat, TableExportObject, TableExportRequest,
    TableImportRequest, TableImportTarget, TriggerScript, TriggerScriptRequest,
    plan_database_action, plan_table_export, trigger_script,
};
use choscordb_driver_api::{
    ConnectionId, ConnectionOptions, EditBatch, ErrorKind, GridCellExpression, GridEditColumn,
    GridEditDriver, GridEditRequest, GridEditRow, ImportConflict, MetadataAvailability, ObjectId,
    ObjectKind, PageSize, QueryOptions, SchemaObject, Secret, SetValueKind, SetValueOutcome,
    StructureChange, StructureChangeKind, StructureColumn, StructureTable, TlsMode, Value,
    grid_set_value, plan_grid_edits, plan_structure,
};
use choscordb_sql_language::{MysqlSqlMode, statement_ranges_mysql};
#[path = "../../driver-mysql/tests/support/server.rs"]
mod server;
use std::collections::BTreeMap;
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

fn table_object(
    engine: &mut Engine,
    connection: ConnectionId,
    database: &str,
    name: &str,
) -> SchemaObject {
    let parent = ObjectId(serde_json::to_string(&[database]).unwrap());
    children(engine, connection, Some(parent))
        .into_iter()
        .find(|object| object.name == name)
        .unwrap_or_else(|| panic!("{database}.{name} is not in the navigator"))
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
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn structure_changes_add_change_index_reference_and_drop_columns() {
    let (mut engine, connection) = connect();
    for (token, sql) in [
        (41, "DROP TABLE IF EXISTS structure_live"),
        (42, "DROP TABLE IF EXISTS structure_live_parent"),
        (
            43,
            "CREATE TABLE structure_live_parent (id INT PRIMARY KEY) ENGINE=InnoDB",
        ),
        (
            44,
            "CREATE TABLE structure_live (id INT PRIMARY KEY, name VARCHAR(64), legacy TEXT, parent_id INT) ENGINE=InnoDB",
        ),
    ] {
        applied(&mut engine, connection, sql, token);
    }
    let table = StructureTable {
        qualified_name: "`choscordb_test`.`structure_live`".into(),
        new_table: false,
        columns: vec![
            structure_column("id", "int", false),
            structure_column("name", "varchar(64)", true),
            structure_column("legacy", "text", true),
            structure_column("parent_id", "int", true),
        ],
        indexes: vec![],
        keys: vec![],
    };
    let statements = plan_structure(
        &table,
        server::driver().structure_dialect().unwrap(),
        &[
            structure_change(
                StructureChangeKind::AddColumn,
                &[
                    ("name", "score"),
                    ("type", "INT"),
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
                    ("ref_table", "choscordb_test.structure_live_parent"),
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
            "SELECT COLUMN_NAME, IS_NULLABLE, DATA_TYPE FROM information_schema.COLUMNS \
             WHERE TABLE_SCHEMA = 'choscordb_test' AND TABLE_NAME = 'structure_live' \
             ORDER BY ORDINAL_POSITION"
        ),
        [
            [text("id"), text("NO"), text("int")],
            [text("full_name"), text("NO"), text("varchar")],
            [text("parent_id"), text("YES"), text("int")],
            [text("score"), text("NO"), text("int")],
        ]
    );
    assert_eq!(
        rows(
            &mut engine,
            connection,
            "SELECT COLUMN_DEFAULT, COLUMN_COMMENT FROM information_schema.COLUMNS \
             WHERE TABLE_SCHEMA = 'choscordb_test' AND TABLE_NAME = 'structure_live' \
             AND COLUMN_NAME = 'score'"
        ),
        [[text("7"), text("Live score")]]
    );
    assert_eq!(
        scalar(
            &mut engine,
            connection,
            "SELECT GROUP_CONCAT(COLUMN_NAME) FROM information_schema.STATISTICS \
             WHERE TABLE_SCHEMA = 'choscordb_test' AND TABLE_NAME = 'structure_live' \
             AND INDEX_NAME = 'structure_live_score'"
        ),
        text("score")
    );
    assert_eq!(
        scalar(
            &mut engine,
            connection,
            "SELECT DELETE_RULE FROM information_schema.REFERENTIAL_CONSTRAINTS \
             WHERE CONSTRAINT_SCHEMA = 'choscordb_test' AND CONSTRAINT_NAME = 'structure_live_parent_fk'"
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
        driver: GridEditDriver::Mysql,
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

fn people(engine: &mut Engine, connection: ConnectionId) -> Vec<Vec<Value>> {
    rows(
        engine,
        connection,
        "SELECT id, name FROM import_live ORDER BY id",
    )
}

fn person(id: i64, name: &str) -> Vec<Value> {
    vec![Value::Integer(id), text(name)]
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
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
        "CREATE TABLE import_live (id INT PRIMARY KEY, name VARCHAR(32) NOT NULL) ENGINE=InnoDB",
        52,
    );
    applied(
        &mut engine,
        connection,
        "INSERT INTO import_live VALUES (1, 'Old')",
        53,
    );
    let existing = TableImportTarget::Existing {
        object: table_object(&mut engine, connection, "choscordb_test", "import_live").id,
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
        message.contains("Column 'name' cannot be null"),
        "{message}"
    );
    assert_eq!(
        people(&mut engine, connection),
        [person(1, "Old"), person(2, "Ann"), person(3, "Bo")]
    );
    let (row, message) = run(&mut engine, "id,name\n1,New\n", ImportConflict::Stop).unwrap_err();
    assert_eq!(row, Some(1), "{message}");
    assert!(message.contains("Duplicate entry '1'"), "{message}");
    run(&mut engine, "id,name\n1,New\n5,Eve\n", ImportConflict::Skip).unwrap();
    assert_eq!(
        people(&mut engine, connection),
        [
            person(1, "Old"),
            person(2, "Ann"),
            person(3, "Bo"),
            person(5, "Eve")
        ]
    );
    run(&mut engine, "id,name\n1,New\n", ImportConflict::Update).unwrap();
    run(&mut engine, "id,name\n5,Fay\n", ImportConflict::Replace).unwrap();
    assert_eq!(
        people(&mut engine, connection),
        [
            person(1, "New"),
            person(2, "Ann"),
            person(3, "Bo"),
            person(5, "Fay")
        ]
    );
    applied(&mut engine, connection, "DROP TABLE import_live", 54);
}

fn sql_mode() -> Option<MysqlSqlMode> {
    Some(MysqlSqlMode {
        mariadb: server::mariadb(),
        ..MysqlSqlMode::default()
    })
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn whole_tables_export_as_csv_files_and_as_a_sql_script_that_runs_again() {
    let (mut engine, connection) = connect();
    let dir = tempfile::tempdir().unwrap();
    for (token, sql) in [
        (61, "DROP TABLE IF EXISTS export_live_people"),
        (62, "DROP TABLE IF EXISTS export_live_notes"),
        (
            63,
            "CREATE TABLE export_live_people (id INT PRIMARY KEY, name VARCHAR(32)) ENGINE=InnoDB",
        ),
        (
            64,
            "INSERT INTO export_live_people VALUES (1, 'Ann'), (2, NULL)",
        ),
        (
            65,
            "CREATE TABLE export_live_notes (body VARCHAR(32)) ENGINE=InnoDB",
        ),
        (66, "INSERT INTO export_live_notes VALUES ('a;b')"),
    ] {
        applied(&mut engine, connection, sql, token);
    }
    let objects: Vec<_> = ["export_live_people", "export_live_notes"]
        .into_iter()
        .map(|name| {
            let object = table_object(&mut engine, connection, "choscordb_test", name);
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
            driver: driver_name().into(),
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
        std::fs::read_to_string(dir.path().join("choscordb_test-export_live_people.csv")).unwrap(),
        "\"id\";\"name\"\r\n\"1\";\"Ann\"\r\n\"2\";NULL\r\n"
    );
    assert_eq!(
        std::fs::read_to_string(dir.path().join("choscordb_test-export_live_notes.csv")).unwrap(),
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
    let sql = std::fs::read_to_string(&script).unwrap();
    let lines: Vec<_> = sql
        .lines()
        .filter(|line| line.starts_with("DROP ") || line.starts_with("INSERT "))
        .collect();
    assert_eq!(
        lines,
        [
            "DROP TABLE `choscordb_test`.`export_live_people`;",
            "INSERT INTO `choscordb_test`.`export_live_people` (`id`, `name`) VALUES (1, CONVERT(X'416e6e' USING utf8mb4));",
            "INSERT INTO `choscordb_test`.`export_live_people` (`id`, `name`) VALUES (2, NULL);",
            "DROP TABLE `choscordb_test`.`export_live_notes`;",
            "INSERT INTO `choscordb_test`.`export_live_notes` (`body`) VALUES (CONVERT(X'613b62' USING utf8mb4));",
        ],
        "{sql}"
    );
    assert_eq!(sql.matches("CREATE TABLE ").count(), 2, "{sql}");
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
                mysql: sql_mode(),
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
            "SELECT id, name FROM export_live_people ORDER BY id"
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
    let mut options = server::options(TlsMode::Disable);
    if let ConnectionOptions::Mysql { database: name, .. }
    | ConnectionOptions::Mariadb { database: name, .. } = &mut options
    {
        *name = database.into();
    }
    options
}

fn switched(engine: &mut Engine, connection: ConnectionId, token: u64) -> Result<(), String> {
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

fn switch(
    engine: &mut Engine,
    connection: ConnectionId,
    database: &str,
    token: u64,
) -> Result<(), String> {
    engine
        .switch_database(connection, options_for(database), token)
        .unwrap();
    switched(engine, connection, token)
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn a_connection_switches_the_database_in_place() {
    let (mut engine, connection) = connect();
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
        scalar(&mut engine, connection, "SELECT DATABASE()"),
        text("switch_live_target")
    );
    assert!(
        switch(&mut engine, connection, "switch_live_missing", 74).is_err(),
        "a missing database switched"
    );
    assert_eq!(
        scalar(&mut engine, connection, "SELECT DATABASE()"),
        text("switch_live_target")
    );
    assert_eq!(
        switch(&mut engine, connection, "choscordb_test", 75),
        Ok(())
    );
    assert_eq!(
        scalar(&mut engine, connection, "SELECT DATABASE()"),
        text("choscordb_test")
    );
    applied(
        &mut engine,
        connection,
        "DROP DATABASE switch_live_target",
        76,
    );
}

fn read_only_profile() -> ConnectionProfile {
    let tls = PostgresTls {
        mode: TlsMode::Disable,
        root_certificate_path: None,
        client_identity_path: None,
    };
    let (host, port, database, user) = (
        "127.0.0.1".to_owned(),
        server::port(),
        "choscordb_test".to_owned(),
        "root".to_owned(),
    );
    ConnectionProfile {
        id: "read-only-live".into(),
        name: "Read-only live".into(),
        group_id: None,
        configuration: if server::mariadb() {
            ProfileConfiguration::Mariadb {
                proxy: None,
                host,
                port,
                database,
                user,
                tls,
                ssh: None,
            }
        } else {
            ProfileConfiguration::Mysql {
                proxy: None,
                host,
                port,
                database,
                user,
                tls,
                ssh: None,
            }
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

fn password() -> Secret {
    Secret::new("choscordb-test-password")
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
#[ignore = "requires disposable MySQL server on localhost:33306"]
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
        "CREATE TABLE read_only_live (id INT) ENGINE=InnoDB",
        82,
    );
    let locked = engine
        .connect_profile(read_only_profile(), Some(password()))
        .unwrap();
    until(
        &mut engine,
        |event| matches!(event, Event::Connected { connection: c, .. } if *c == locked),
    );
    let rejected = if server::mariadb() {
        "Cannot execute statement in a READ ONLY transaction"
    } else {
        "Cannot execute statement in a READ ONLY transaction."
    };
    assert_eq!(
        statement_error(&mut engine, locked, "INSERT INTO read_only_live VALUES (1)"),
        rejected
    );
    assert_eq!(
        statement_error(
            &mut engine,
            locked,
            "/* plain */ UPDATE read_only_live SET id = 2"
        ),
        rejected
    );
    assert_eq!(
        statement_error(
            &mut engine,
            locked,
            "CREATE TABLE read_only_other (id INT) ENGINE=InnoDB"
        ),
        rejected
    );
    assert_eq!(
        scalar(&mut engine, locked, "SELECT COUNT(*) FROM read_only_live"),
        Value::Integer(0)
    );
    engine
        .switch_profile_database(
            locked,
            read_only_profile(),
            ProfileSecrets {
                database: Some(password()),
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
    assert_eq!(switched(&mut engine, locked, 85), Ok(()));
    assert_eq!(
        statement_error(&mut engine, locked, "INSERT INTO read_only_live VALUES (2)"),
        rejected
    );
    applied(&mut engine, connection, "DROP TABLE read_only_live", 84);
}

fn trigger_names(
    engine: &mut Engine,
    connection: ConnectionId,
    table: &SchemaObject,
) -> Vec<(String, String)> {
    children(engine, connection, Some(table.id.clone()))
        .into_iter()
        .filter(|object| object.kind == ObjectKind::Trigger)
        .map(|object| {
            let definition = object
                .properties
                .iter()
                .find(|property| property.name == "Definition")
                .map(|property| property.value.clone())
                .unwrap_or_else(|| panic!("{} has no definition", object.name));
            (object.name, definition)
        })
        .collect()
}

fn run_script(engine: &mut Engine, connection: ConnectionId, script: &str, token: u64) {
    let statements: Vec<String> = statement_ranges_mysql(script)
        .into_iter()
        .map(|range| script[range].trim().to_owned())
        .collect();
    engine.apply_ddl(connection, statements, token).unwrap();
    let event = until(engine, |event| {
        matches!(event,
            Event::EditApplied { request_token, .. } | Event::EditFailed { request_token, .. }
                if *request_token == token)
    });
    assert!(
        matches!(event, Event::EditApplied { .. }),
        "{script}: {event:?}"
    );
}

fn edit_a_trigger(engine: &mut Engine, connection: ConnectionId, database: &str, token: u64) {
    let table = table_object(engine, connection, database, "trigger_live");
    let request = |trigger: &str, definition: &str| TriggerScriptRequest {
        driver: driver_name().into(),
        table_id: table.id.0.clone(),
        table_name: table.qualified_name.clone(),
        trigger: trigger.into(),
        definition: definition.into(),
    };
    let create = trigger_script(TriggerScript::Create, &request("", "")).unwrap();
    run_script(engine, connection, &create, token);
    let definition = format!(
        "CREATE TRIGGER `{database}`.`trigger_live_trigger` BEFORE INSERT ON `{database}`.`trigger_live` \
         FOR EACH ROW BEGIN\n  SET @changed_at = NOW();\nEND"
    );
    assert_eq!(
        trigger_names(engine, connection, &table),
        [("trigger_live_trigger".to_owned(), definition.clone())]
    );
    let edited = definition.replace("SET @changed_at = NOW();", "SET NEW.id = NEW.id + 100;");
    let edit = trigger_script(
        TriggerScript::Edit,
        &request("trigger_live_trigger", &edited),
    )
    .unwrap();
    run_script(engine, connection, &edit, token + 1);
    assert_eq!(
        trigger_names(engine, connection, &table),
        [("trigger_live_trigger".to_owned(), edited)]
    );
    applied(
        engine,
        connection,
        &format!("INSERT INTO `{database}`.`trigger_live` VALUES (1)"),
        token + 2,
    );
    assert_eq!(
        scalar(
            engine,
            connection,
            &format!("SELECT id FROM `{database}`.`trigger_live`")
        ),
        Value::Integer(101)
    );
    let drop = trigger_script(TriggerScript::Drop, &request("trigger_live_trigger", "")).unwrap();
    run_script(engine, connection, &drop, token + 3);
    assert!(trigger_names(engine, connection, &table).is_empty());
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn trigger_scripts_create_list_edit_and_drop_a_trigger_in_each_database() {
    let (mut engine, connection) = connect();
    for (token, sql) in [
        (101, "DROP TABLE IF EXISTS trigger_live"),
        (
            102,
            "CREATE TABLE trigger_live (id INT PRIMARY KEY) ENGINE=InnoDB",
        ),
        (103, "DROP DATABASE IF EXISTS trigger_live_other"),
        (104, "CREATE DATABASE trigger_live_other"),
        (
            105,
            "CREATE TABLE trigger_live_other.trigger_live (id INT PRIMARY KEY) ENGINE=InnoDB",
        ),
    ] {
        applied(&mut engine, connection, sql, token);
    }
    edit_a_trigger(&mut engine, connection, "choscordb_test", 110);
    edit_a_trigger(&mut engine, connection, "trigger_live_other", 120);
    assert_eq!(
        scalar(&mut engine, connection, "SELECT DATABASE()"),
        text("choscordb_test")
    );
    applied(&mut engine, connection, "DROP TABLE trigger_live", 106);
    applied(
        &mut engine,
        connection,
        "DROP DATABASE trigger_live_other",
        107,
    );
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
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
        "CREATE TABLE set_value_live (id INT PRIMARY KEY, label VARCHAR(32) DEFAULT 'fallback', \
         stamp DATETIME, day DATE) ENGINE=InnoDB",
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
        GridEditDriver::Mysql,
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
        driver: GridEditDriver::Mysql,
        qualified_name: "`choscordb_test`.`set_value_live`".into(),
        parameter_style: "?".into(),
        reason: String::new(),
        object_read_only: false,
        columns: vec![
            column("id", "int", true),
            column("label", "varchar(32)", false),
            column("stamp", "datetime", false),
            column("day", "date", false),
        ],
        rows: vec![
            GridEditRow {
                current: vec![Value::Integer(7), text("typed"), Value::Null, Value::Null],
                original: vec![Value::Integer(7), text("typed"), Value::Null, Value::Null],
                touched: vec![false, true, true, true],
                expressions: vec![
                    None,
                    expression(SetValueKind::Default, "varchar(32)", false),
                    expression(SetValueKind::Now, "datetime", false),
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
                    expression(SetValueKind::Default, "varchar(32)", true),
                    expression(SetValueKind::Now, "datetime", true),
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
            "SELECT id, label, stamp IS NOT NULL, day = DATE(stamp) FROM set_value_live ORDER BY id"
        ),
        [
            vec![
                Value::Integer(7),
                text("fallback"),
                Value::Integer(1),
                Value::Integer(1)
            ],
            vec![
                Value::Integer(8),
                text("fallback"),
                Value::Integer(1),
                Value::Null
            ],
        ]
    );
    applied(&mut engine, connection, "DROP TABLE set_value_live", 95);
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn a_sql_table_export_restores_its_own_database_from_another_database() {
    let (mut engine, connection) = connect();
    let dir = tempfile::tempdir().unwrap();
    for (token, sql) in [
        (131, "DROP TABLE IF EXISTS export_home_people"),
        (132, "DROP DATABASE IF EXISTS export_live_elsewhere"),
        (
            133,
            "CREATE TABLE export_home_people (id INT PRIMARY KEY, name VARCHAR(32)) ENGINE=InnoDB",
        ),
        (134, "INSERT INTO export_home_people VALUES (1, 'Ann')"),
        (135, "CREATE DATABASE export_live_elsewhere"),
    ] {
        applied(&mut engine, connection, sql, token);
    }
    let object = table_object(
        &mut engine,
        connection,
        "choscordb_test",
        "export_home_people",
    );
    let script = dir.path().join("home.sql");
    let targets = plan_table_export(&TableExportRequest {
        driver: driver_name().into(),
        objects: vec![TableExportObject {
            object_id: object.id.0,
            qualified_name: object.qualified_name,
            kind: "table".into(),
            relation_subtype: String::new(),
        }],
        format: TableExportFormat::Sql {
            structure: true,
            drop: true,
            data: true,
        },
        destination: TableExportDestination::File(script.clone()),
    })
    .unwrap();
    engine.export_tables(connection, targets).unwrap();
    let event = until(&mut engine, |event| {
        matches!(
            event,
            Event::TableExportFinished { .. } | Event::TableExportFailed { .. }
        )
    });
    assert!(
        matches!(
            event,
            Event::TableExportFinished {
                tables: 1,
                rows: 1,
                ..
            }
        ),
        "{event:?}"
    );
    assert_eq!(
        switch(&mut engine, connection, "export_live_elsewhere", 136),
        Ok(())
    );
    engine
        .run_sql_file(
            connection,
            SqlFileRun {
                file: script.clone(),
                mysql: sql_mode(),
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
    let outcome = match &event {
        Event::SqlFileFinished { failed: 0, .. } => Ok(()),
        Event::SqlFileFailed { error, .. } => Err(error.message.clone()),
        event => Err(format!("{event:?}")),
    };
    let elsewhere = scalar(
        &mut engine,
        connection,
        "SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA = 'export_live_elsewhere'",
    );
    let restored = scalar(
        &mut engine,
        connection,
        "SELECT COUNT(*) FROM information_schema.TABLES \
         WHERE TABLE_SCHEMA = 'choscordb_test' AND TABLE_NAME = 'export_home_people'",
    );
    assert_eq!(
        switch(&mut engine, connection, "choscordb_test", 137),
        Ok(())
    );
    applied(
        &mut engine,
        connection,
        "DROP DATABASE export_live_elsewhere",
        138,
    );
    applied(
        &mut engine,
        connection,
        "DROP TABLE IF EXISTS export_home_people",
        139,
    );
    let sql = std::fs::read_to_string(&script).unwrap();
    assert_eq!(outcome, Ok(()), "{sql}");
    assert_eq!(elsewhere, Value::Integer(0), "{sql}");
    assert_eq!(restored, Value::Integer(1), "{sql}");
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn an_import_counts_each_written_file_row_once_for_each_conflict_policy() {
    let (mut engine, connection) = connect();
    let dir = tempfile::tempdir().unwrap();
    applied(
        &mut engine,
        connection,
        "DROP TABLE IF EXISTS import_live",
        141,
    );
    applied(
        &mut engine,
        connection,
        "CREATE TABLE import_live (id INT PRIMARY KEY, name VARCHAR(32) NOT NULL) ENGINE=InnoDB",
        142,
    );
    applied(
        &mut engine,
        connection,
        "INSERT INTO import_live VALUES (1, 'Old'), (2, 'Ann')",
        143,
    );
    let existing = TableImportTarget::Existing {
        object: table_object(&mut engine, connection, "choscordb_test", "import_live").id,
    };
    let counts: Vec<_> = [
        ("id,name\n1,Old\n5,Eve\n", ImportConflict::Skip),
        ("id,name\n1,New\n", ImportConflict::Update),
        ("id,name\n2,Bo\n", ImportConflict::Replace),
    ]
    .into_iter()
    .map(|(body, conflict)| {
        (
            conflict,
            import(
                &mut engine,
                connection,
                dir.path(),
                body,
                existing.clone(),
                conflict,
            ),
        )
    })
    .collect();
    applied(&mut engine, connection, "DROP TABLE import_live", 144);
    assert_eq!(
        counts,
        [
            (ImportConflict::Skip, Ok(1)),
            (ImportConflict::Update, Ok(1)),
            (ImportConflict::Replace, Ok(1)),
        ]
    );
}
