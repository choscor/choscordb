use choscordb_core::{
    CsvOptions, ExportFormat, SqlDialect, TableExportDestination, TableExportFormat,
    TableExportObject, TableExportRequest, plan_table_export,
};
use std::path::PathBuf;

fn table(object_id: &str, qualified_name: &str) -> TableExportObject {
    TableExportObject {
        object_id: object_id.into(),
        qualified_name: qualified_name.into(),
        kind: "table".into(),
        relation_subtype: String::new(),
    }
}

fn folder(pattern: &str) -> TableExportDestination {
    TableExportDestination::Folder {
        path: PathBuf::from("/exports"),
        pattern: pattern.into(),
    }
}

fn request(
    driver: &str,
    objects: Vec<TableExportObject>,
    format: TableExportFormat,
    destination: TableExportDestination,
) -> TableExportRequest {
    TableExportRequest {
        driver: driver.into(),
        objects,
        format,
        destination,
    }
}

#[test]
fn each_table_gets_a_select_and_a_file_from_the_pattern() {
    let plan = plan_table_export(&request(
        "postgres",
        vec![
            table("pg:relation:1", "\"public\".\"orders\""),
            table("pg:relation:2", "\"sales\".\"a/b\""),
        ],
        TableExportFormat::Csv(CsvOptions::default()),
        folder("{schema}-{table}"),
    ))
    .unwrap();
    assert_eq!(plan.len(), 2);
    assert_eq!(plan[0].label, "public.orders");
    assert_eq!(
        plan[0].destination,
        PathBuf::from("/exports/public-orders.csv")
    );
    assert_eq!(plan[1].destination, PathBuf::from("/exports/sales-a_b.csv"));
    let data = plan[0].data.as_ref().unwrap();
    assert_eq!(data.select, "SELECT * FROM \"public\".\"orders\";");
    assert!(matches!(data.format, ExportFormat::Csv(_)));
    assert!(plan[0].drop.is_none() && !plan[0].structure);
}

#[test]
fn sql_exports_add_drop_and_structure_and_can_share_one_file() {
    let plan = plan_table_export(&request(
        "mysql",
        vec![
            table("[\"shop\",\"orders\"]", "ignored"),
            table("[\"shop\",\"items\"]", "ignored"),
        ],
        TableExportFormat::Sql {
            structure: true,
            drop: true,
            data: true,
        },
        TableExportDestination::File(PathBuf::from("/exports/shop.sql")),
    ))
    .unwrap();
    assert_eq!(plan.len(), 2);
    assert!(
        plan.iter()
            .all(|target| target.destination.as_path() == std::path::Path::new("/exports/shop.sql"))
    );
    assert_eq!(plan[0].drop.as_deref(), Some("DROP TABLE `shop`.`orders`;"));
    assert!(plan[0].structure);
    match &plan[1].data.as_ref().unwrap().format {
        ExportFormat::SqlInsert { table, dialect } => {
            assert_eq!(table, &["shop".to_string(), "items".to_string()]);
            assert_eq!(*dialect, SqlDialect::Mysql);
        }
        _ => panic!("expected SQL INSERT"),
    }
    let structure_only = plan_table_export(&request(
        "sqlite",
        vec![table("[\"main\",\"t\"]", "t")],
        TableExportFormat::Sql {
            structure: true,
            drop: false,
            data: false,
        },
        folder("{table}"),
    ))
    .unwrap();
    assert!(structure_only[0].data.is_none());
    assert_eq!(
        structure_only[0].destination,
        PathBuf::from("/exports/t.sql")
    );
}

#[test]
fn invalid_requests_return_the_reason() {
    let csv = || TableExportFormat::Csv(CsvOptions::default());
    let two = || {
        vec![
            table("[\"main\",\"a\"]", "a"),
            table("[\"main\",\"b\"]", "b"),
        ]
    };
    let cases = [
        (
            request("sqlite", vec![], csv(), folder("{table}")),
            "Choose one or more tables to export.",
        ),
        (
            request(
                "sqlite",
                vec![TableExportObject {
                    kind: "index".into(),
                    ..table("[\"main\",\"i\"]", "i")
                }],
                csv(),
                folder("{table}"),
            ),
            "Only tables and views can be exported.",
        ),
        (
            request(
                "sqlite",
                two(),
                csv(),
                TableExportDestination::File(PathBuf::from("/exports/all.csv")),
            ),
            "Choose a folder to export several tables as CSV, JSON or Excel.",
        ),
        (
            request("sqlite", two(), csv(), folder("export")),
            "Two tables would write the same file. Add {table} or {schema} to the file name pattern.",
        ),
        (
            request("sqlite", two(), csv(), folder("x/{table}")),
            "The file name pattern cannot contain a folder.",
        ),
        (
            request(
                "sqlite",
                two(),
                TableExportFormat::Sql {
                    structure: false,
                    drop: false,
                    data: false,
                },
                folder("{table}"),
            ),
            "Choose structure, drop or data for the SQL export.",
        ),
        (
            request(
                "sqlite",
                two(),
                TableExportFormat::Csv(CsvOptions {
                    quote: ',',
                    ..CsvOptions::default()
                }),
                folder("{table}"),
            ),
            "The delimiter and the quote must be different.",
        ),
    ];
    for (request, message) in cases {
        assert_eq!(plan_table_export(&request).unwrap_err(), message);
    }
}

mod engine_export {
    use super::*;
    use choscordb_core::{Engine, Event};
    use choscordb_driver_api::{ConnectionOptions, ErrorKind};
    use choscordb_driver_sqlite::SqliteDriver;
    use std::sync::Arc;
    use std::time::{Duration, Instant};

    fn wait(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) -> Event {
        let until = Instant::now() + Duration::from_secs(10);
        loop {
            if let Some(event) = engine.try_event()
                && predicate(&event)
            {
                return event;
            }
            assert!(Instant::now() < until, "event timed out");
            std::thread::sleep(Duration::from_millis(1));
        }
    }

    fn setup(dir: &tempfile::TempDir) -> (Engine, choscordb_driver_api::Handle) {
        let path = dir.path().join("source.sqlite");
        rusqlite::Connection::open(&path)
            .unwrap()
            .execute_batch(
                "CREATE TABLE people (id INTEGER, name TEXT);
                 INSERT INTO people VALUES (1, 'Ann'), (2, NULL);
                 CREATE TABLE notes (body TEXT);
                 INSERT INTO notes VALUES ('a;b');",
            )
            .unwrap();
        let mut engine = Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
        let connection = engine
            .connect(
                "sqlite",
                ConnectionOptions::Sqlite {
                    path,
                    read_only: false,
                },
            )
            .unwrap();
        wait(&mut engine, |event| {
            matches!(event, Event::Connected { .. })
        });
        (engine, connection)
    }

    fn finish(engine: &mut Engine) -> Event {
        wait(engine, |event| {
            matches!(
                event,
                Event::TableExportFinished { .. } | Event::TableExportFailed { .. }
            )
        })
    }

    #[test]
    fn tables_export_to_one_file_each_with_csv_options() {
        let dir = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let (mut engine, connection) = setup(&source);
        let targets = plan_table_export(&request(
            "sqlite",
            vec![
                table("[\"main\",\"people\"]", "people"),
                table("[\"main\",\"notes\"]", "notes"),
            ],
            TableExportFormat::Csv(CsvOptions {
                delimiter: ';',
                null_text: "NULL".into(),
                ..CsvOptions::default()
            }),
            TableExportDestination::Folder {
                path: dir.path().into(),
                pattern: "{table}".into(),
            },
        ))
        .unwrap();
        let export = engine.export_tables(connection, targets).unwrap();
        let progress = wait(&mut engine, |event| {
            matches!(event, Event::TableExportProgress { .. })
        });
        assert!(
            matches!(progress, Event::TableExportProgress { export: id, completed: 1, total: 2, rows: 2 } if id == export),
            "{progress:?}"
        );
        let event = finish(&mut engine);
        assert!(
            matches!(event, Event::TableExportFinished { export: id, tables: 2, rows: 3 } if id == export),
            "{event:?}"
        );
        assert_eq!(
            std::fs::read_to_string(dir.path().join("people.csv")).unwrap(),
            "\"id\";\"name\"\r\n\"1\";\"Ann\"\r\n\"2\";NULL\r\n"
        );
        assert_eq!(
            std::fs::read_to_string(dir.path().join("notes.csv")).unwrap(),
            "\"body\"\r\n\"a;b\"\r\n"
        );
    }

    #[test]
    fn sql_export_writes_drop_structure_and_rows_into_one_file() {
        let dir = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let (mut engine, connection) = setup(&source);
        let path = dir.path().join("all.sql");
        let targets = plan_table_export(&request(
            "sqlite",
            vec![
                table("[\"main\",\"people\"]", "people"),
                table("[\"main\",\"notes\"]", "notes"),
            ],
            TableExportFormat::Sql {
                structure: true,
                drop: true,
                data: true,
            },
            TableExportDestination::File(path.clone()),
        ))
        .unwrap();
        engine.export_tables(connection, targets).unwrap();
        let event = finish(&mut engine);
        assert!(
            matches!(event, Event::TableExportFinished { rows: 3, .. }),
            "{event:?}"
        );
        assert_eq!(
            std::fs::read_to_string(path).unwrap(),
            "DROP TABLE \"main\".\"people\";\nCREATE TABLE people (id INTEGER, name TEXT);\n\
             INSERT INTO \"main\".\"people\" (\"id\", \"name\") VALUES (1, 'Ann');\n\
             INSERT INTO \"main\".\"people\" (\"id\", \"name\") VALUES (2, NULL);\n\
             DROP TABLE \"main\".\"notes\";\nCREATE TABLE notes (body TEXT);\n\
             INSERT INTO \"main\".\"notes\" (\"body\") VALUES ('a;b');\n"
        );
    }

    #[test]
    fn a_failed_table_names_the_table_and_keeps_no_partial_file() {
        let dir = tempfile::tempdir().unwrap();
        let source = tempfile::tempdir().unwrap();
        let (mut engine, connection) = setup(&source);
        let targets = plan_table_export(&request(
            "sqlite",
            vec![table("[\"main\",\"missing\"]", "missing")],
            TableExportFormat::Json,
            TableExportDestination::Folder {
                path: dir.path().into(),
                pattern: "{table}".into(),
            },
        ))
        .unwrap();
        engine.export_tables(connection, targets.clone()).unwrap();
        assert_eq!(
            engine.export_tables(connection, targets),
            Err(choscordb_core::SubmitError::ResourceLimit)
        );
        let event = finish(&mut engine);
        assert!(
            matches!(&event, Event::TableExportFailed { table, error, .. } if table == "main.missing" && error.kind != ErrorKind::Cancelled),
            "{event:?}"
        );
        assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 0);
    }
}

#[test]
fn excel_exports_write_one_workbook_per_table() {
    let plan = plan_table_export(&request(
        "sqlite",
        vec![table("[\"main\",\"t\"]", "t")],
        TableExportFormat::Xlsx,
        folder("{table}"),
    ))
    .unwrap();
    assert_eq!(plan[0].destination, PathBuf::from("/exports/t.xlsx"));
    assert!(matches!(
        plan[0].data.as_ref().unwrap().format,
        ExportFormat::Xlsx
    ));
}
