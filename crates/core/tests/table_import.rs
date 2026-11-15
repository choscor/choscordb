use choscordb_core::{
    CsvReadOptions, ImportData, ImportEncoding, ImportFile, ImportFileFormat, ImportMapping,
    ImportRead, TableImportRequest, TableImportTarget, plan_table_import, read_import_file,
};
use choscordb_driver_api::{
    EditColumn, EditTarget, ErrorKind, GridEditDriver, ImportConflict, ObjectId, Value,
};
use std::path::Path;

fn write(dir: &Path, name: &str, bytes: &[u8]) -> std::path::PathBuf {
    let path = dir.join(name);
    std::fs::write(&path, bytes).unwrap();
    path
}

fn csv(path: std::path::PathBuf, options: CsvReadOptions) -> ImportFile {
    ImportFile {
        path,
        format: ImportFileFormat::Csv(options),
        encoding: ImportEncoding::Utf8,
    }
}

fn json(path: std::path::PathBuf) -> ImportFile {
    ImportFile {
        path,
        format: ImportFileFormat::Json,
        encoding: ImportEncoding::Utf8,
    }
}

fn text(value: &str) -> Option<String> {
    Some(value.into())
}

#[test]
fn csv_reads_the_header_quoted_fields_and_the_null_text() {
    let dir = tempfile::tempdir().unwrap();
    let path = write(
        dir.path(),
        "people.csv",
        "\u{feff}id;name;\n1;'Ann; Lee';\\N\n2;'It''s';x\n".as_bytes(),
    );
    let data = read_import_file(
        &csv(
            path,
            CsvReadOptions {
                delimiter: b';',
                quote: b'\'',
                header: true,
                null_text: "\\N".into(),
            },
        ),
        ImportRead::All,
    )
    .unwrap();
    assert_eq!(data.columns, vec!["id", "name", "column_3"]);
    assert_eq!(
        data.rows,
        vec![
            vec![text("1"), text("Ann; Lee"), None],
            vec![text("2"), text("It's"), text("x")],
        ]
    );
}

#[test]
fn csv_without_a_header_names_columns_and_keeps_the_first_row() {
    let dir = tempfile::tempdir().unwrap();
    let path = write(dir.path(), "plain.csv", b"1,,b\n");
    let data = read_import_file(
        &csv(
            path,
            CsvReadOptions {
                header: false,
                ..CsvReadOptions::default()
            },
        ),
        ImportRead::All,
    )
    .unwrap();
    assert_eq!(data.columns, vec!["column_1", "column_2", "column_3"]);
    assert_eq!(data.rows, vec![vec![text("1"), None, text("b")]]);
}

#[test]
fn csv_rejects_a_row_with_a_different_field_count_and_names_the_row() {
    let dir = tempfile::tempdir().unwrap();
    let path = write(dir.path(), "ragged.csv", b"a,b\n1,2\n3\n");
    let error = read_import_file(&csv(path, CsvReadOptions::default()), ImportRead::All)
        .err()
        .unwrap();
    assert_eq!(
        error.message,
        "Row 2 has 1 fields. The first row has 2 fields."
    );
}

#[test]
fn preview_stops_at_one_hundred_rows_and_import_reads_every_row() {
    let dir = tempfile::tempdir().unwrap();
    let mut body = String::from("n\n");
    for row in 0..150 {
        body.push_str(&format!("{row}\n"));
    }
    let path = write(dir.path(), "many.csv", body.as_bytes());
    let file = csv(path, CsvReadOptions::default());
    assert_eq!(
        read_import_file(&file, ImportRead::Preview)
            .unwrap()
            .rows
            .len(),
        100
    );
    assert_eq!(
        read_import_file(&file, ImportRead::All).unwrap().rows.len(),
        150
    );
}

#[test]
fn encoding_choice_decodes_latin1_and_rejects_invalid_utf8() {
    let dir = tempfile::tempdir().unwrap();
    let path = write(dir.path(), "latin.csv", b"name\ncaf\xe9\n");
    let mut file = csv(path, CsvReadOptions::default());
    assert_eq!(
        read_import_file(&file, ImportRead::All)
            .err()
            .unwrap()
            .message,
        "The file is not valid UTF-8. Choose the encoding of the file."
    );
    file.encoding = ImportEncoding::Latin1;
    assert_eq!(
        read_import_file(&file, ImportRead::All).unwrap().rows,
        vec![vec![text("café")]]
    );
}

#[test]
fn json_arrays_keep_the_key_order_and_join_keys_from_all_rows() {
    let dir = tempfile::tempdir().unwrap();
    let path = write(
        dir.path(),
        "rows.json",
        br#"[{"z": 1, "a": "x", "tags": [1, 2]}, {"b": true, "a": null}]"#,
    );
    let data = read_import_file(&json(path), ImportRead::All).unwrap();
    assert_eq!(data.columns, vec!["z", "a", "tags", "b"]);
    assert_eq!(
        data.rows,
        vec![
            vec![text("1"), text("x"), text("[1,2]"), None],
            vec![None, None, None, text("true")],
        ]
    );
}

#[test]
fn json_lines_read_one_object_per_line_and_name_a_bad_row() {
    let dir = tempfile::tempdir().unwrap();
    let path = write(dir.path(), "rows.jsonl", b"{\"a\": 1}\n\n{\"a\": 2}\n");
    assert_eq!(
        read_import_file(&json(path), ImportRead::All).unwrap().rows,
        vec![vec![text("1")], vec![text("2")]]
    );
    let bad = write(dir.path(), "bad.json", b"[{\"a\": 1}, 5]");
    assert_eq!(
        read_import_file(&json(bad), ImportRead::All)
            .err()
            .unwrap()
            .message,
        "Row 2 is not a JSON object."
    );
}

fn target() -> EditTarget {
    EditTarget {
        qualified_name: "\"main\".\"people\"".into(),
        parameter_style: "?".into(),
        columns: vec![
            EditColumn {
                name: "id".into(),
                database_type: "INTEGER".into(),
                nullable: false,
                generated: false,
                key: true,
            },
            EditColumn {
                name: "total".into(),
                database_type: "NUMERIC(10,2)".into(),
                nullable: true,
                generated: true,
                key: false,
            },
            EditColumn {
                name: "name".into(),
                database_type: "TEXT".into(),
                nullable: true,
                generated: false,
                key: false,
            },
        ],
        key_columns: vec!["id".into()],
        reason: String::new(),
    }
}

fn request(target: TableImportTarget, mapping: &[(&str, usize)]) -> TableImportRequest {
    TableImportRequest {
        driver: GridEditDriver::Sqlite,
        file: json("unused.json".into()),
        target,
        mapping: mapping
            .iter()
            .map(|(target, source)| ImportMapping {
                target: (*target).into(),
                source: *source,
            })
            .collect(),
        conflict: ImportConflict::Stop,
    }
}

fn existing() -> TableImportTarget {
    TableImportTarget::Existing {
        object: ObjectId("[\"main\",\"people\"]".into()),
    }
}

fn data() -> ImportData {
    ImportData {
        columns: vec!["full_name".into(), "number".into()],
        rows: vec![vec![text("Ann"), text("7")], vec![None, text("8")]],
    }
}

#[test]
fn plan_maps_file_columns_to_table_columns_in_mapping_order() {
    let plan = plan_table_import(
        &request(existing(), &[("id", 1), ("name", 0)]),
        Some(&target()),
        data(),
    )
    .unwrap();
    assert_eq!(plan.create, None);
    assert_eq!(plan.batch.statements.len(), 2);
    assert_eq!(
        plan.batch.statements[0].sql,
        "INSERT INTO \"main\".\"people\" (\"id\", \"name\") VALUES (?, ?)"
    );
    assert!(matches!(
        plan.batch.statements[1].params.as_slice(),
        [Value::Text(id), Value::Null] if id == "8"
    ));
}

#[test]
fn plan_rejects_bad_mappings_with_a_reason() {
    let message = |mapping: &[(&str, usize)]| {
        plan_table_import(&request(existing(), mapping), Some(&target()), data())
            .err()
            .unwrap()
            .message
    };
    assert_eq!(message(&[]), "Choose one or more columns to import.");
    assert_eq!(
        message(&[("id", 0), ("id", 1)]),
        "The column id receives more than one file column."
    );
    assert_eq!(message(&[("id", 2)]), "The file has no column 3.");
    assert_eq!(message(&[("age", 0)]), "The table has no column age.");
    assert_eq!(
        message(&[("total", 0)]),
        "The column total is generated and cannot receive data."
    );
    let mut locked = target();
    locked.reason = "Only transactional InnoDB base tables can be edited".into();
    assert_eq!(
        plan_table_import(&request(existing(), &[("id", 0)]), Some(&locked), data())
            .err()
            .unwrap()
            .message,
        "Only transactional InnoDB base tables can be edited"
    );
}

#[test]
fn new_table_plan_creates_text_columns_and_a_drop_for_failures() {
    let plan = plan_table_import(
        &request(
            TableImportTarget::New {
                qualified_name: "\"main\".\"imported\"".into(),
            },
            &[("full name", 0)],
        ),
        None,
        data(),
    )
    .unwrap();
    assert_eq!(
        plan.create.as_deref(),
        Some("CREATE TABLE \"main\".\"imported\" (\"full name\" TEXT)")
    );
    assert_eq!(
        plan.drop.as_deref(),
        Some("DROP TABLE \"main\".\"imported\"")
    );
    assert_eq!(plan.batch.statements.len(), 2);
    assert_eq!(
        plan_table_import(
            &request(
                TableImportTarget::New {
                    qualified_name: "\"imported\"".into()
                },
                &[(" ", 0)]
            ),
            None,
            data()
        )
        .err()
        .map(|error| (error.kind, error.message)),
        Some((
            ErrorKind::InvalidInput,
            "Enter a name for each imported column.".into()
        ))
    );
}

mod engine {
    use super::*;
    use choscordb_core::{Engine, Event};
    use choscordb_driver_api::ConnectionOptions;
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

    struct Fixture {
        dir: tempfile::TempDir,
        engine: Engine,
        connection: choscordb_driver_api::Handle,
    }

    impl Fixture {
        fn new() -> Self {
            let dir = tempfile::tempdir().unwrap();
            rusqlite::Connection::open(dir.path().join("db.sqlite"))
                .unwrap()
                .execute_batch(
                    "CREATE TABLE people (id INTEGER PRIMARY KEY, name TEXT NOT NULL);
                     INSERT INTO people VALUES (1, 'Old');",
                )
                .unwrap();
            let mut engine = Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
            let connection = engine
                .connect(
                    "sqlite",
                    ConnectionOptions::Sqlite {
                        path: dir.path().join("db.sqlite"),
                        read_only: false,
                    },
                )
                .unwrap();
            wait(&mut engine, |event| {
                matches!(event, Event::Connected { .. })
            });
            Self {
                dir,
                engine,
                connection,
            }
        }

        fn import(
            &mut self,
            body: &str,
            target: TableImportTarget,
            conflict: ImportConflict,
        ) -> Event {
            let path = write(self.dir.path(), "rows.csv", body.as_bytes());
            let mut request = request(target, &[("id", 0), ("name", 1)]);
            request.file = csv(path, CsvReadOptions::default());
            request.conflict = conflict;
            let import = self.engine.import_table(self.connection, request).unwrap();
            let progress = wait(&mut self.engine, |event| {
                matches!(event, Event::TableImportProgress { .. })
            });
            assert!(
                matches!(progress, Event::TableImportProgress { import: id, .. } if id == import)
            );
            wait(&mut self.engine, |event| {
                matches!(
                    event,
                    Event::TableImportFinished { .. } | Event::TableImportFailed { .. }
                )
            })
        }

        fn rows(&self, sql: &str) -> Vec<(i64, String)> {
            let db = rusqlite::Connection::open(self.dir.path().join("db.sqlite")).unwrap();
            let mut statement = db.prepare(sql).unwrap();
            statement
                .query_map([], |row| Ok((row.get(0)?, row.get(1)?)))
                .unwrap()
                .collect::<Result<_, _>>()
                .unwrap()
        }

        fn people(&self) -> Vec<(i64, String)> {
            self.rows("SELECT id, name FROM people ORDER BY id")
        }
    }

    #[test]
    fn import_inserts_every_row_in_one_transaction() {
        let mut fixture = Fixture::new();
        let event = fixture.import("id,name\n2,Ann\n3,Bo\n", existing(), ImportConflict::Stop);
        assert!(
            matches!(event, Event::TableImportFinished { rows: 2, .. }),
            "{event:?}"
        );
        assert_eq!(
            fixture.people(),
            vec![(1, "Old".into()), (2, "Ann".into()), (3, "Bo".into())]
        );
    }

    #[test]
    fn a_failed_row_rolls_back_the_import_and_reports_its_row_number() {
        let mut fixture = Fixture::new();
        let event = fixture.import(
            "id,name\n2,Ann\n3,\n4,Cy\n",
            existing(),
            ImportConflict::Stop,
        );
        assert!(
            matches!(&event, Event::TableImportFailed { row: Some(2), error, .. } if error.message.contains("NOT NULL")),
            "{event:?}"
        );
        assert_eq!(fixture.people(), vec![(1, "Old".into())]);
    }

    #[test]
    fn conflict_policies_stop_skip_update_and_replace_rows_with_the_same_key() {
        let mut fixture = Fixture::new();
        let event = fixture.import("id,name\n1,New\n", existing(), ImportConflict::Stop);
        assert!(
            matches!(event, Event::TableImportFailed { row: Some(1), .. }),
            "{event:?}"
        );
        let event = fixture.import("id,name\n1,New\n5,Eve\n", existing(), ImportConflict::Skip);
        assert!(
            matches!(event, Event::TableImportFinished { rows: 1, .. }),
            "{event:?}"
        );
        assert_eq!(fixture.people(), vec![(1, "Old".into()), (5, "Eve".into())]);
        fixture.import("id,name\n1,New\n", existing(), ImportConflict::Update);
        assert_eq!(fixture.people(), vec![(1, "New".into()), (5, "Eve".into())]);
        fixture.import("id,name\n5,Fay\n", existing(), ImportConflict::Replace);
        assert_eq!(fixture.people(), vec![(1, "New".into()), (5, "Fay".into())]);
    }

    #[test]
    fn a_new_table_is_created_and_removed_again_when_a_row_fails() {
        let mut fixture = Fixture::new();
        let created = TableImportTarget::New {
            qualified_name: "\"main\".\"imported\"".into(),
        };
        let event = fixture.import("id,name\n7,Gil\n", created.clone(), ImportConflict::Stop);
        assert!(
            matches!(event, Event::TableImportFinished { rows: 1, .. }),
            "{event:?}"
        );
        assert_eq!(
            fixture.rows("SELECT CAST(id AS INTEGER), name FROM imported"),
            vec![(7, "Gil".into())]
        );
        let failed = TableImportTarget::New {
            qualified_name: "\"main\".\"failed\"".into(),
        };
        let event = fixture.import("id,name\n1,a\n", failed, ImportConflict::Update);
        assert!(
            matches!(event, Event::TableImportFailed { row: None, .. }),
            "{event:?}"
        );
        let db = rusqlite::Connection::open(fixture.dir.path().join("db.sqlite")).unwrap();
        let tables: i64 = db
            .query_row(
                "SELECT count(*) FROM sqlite_master WHERE name = 'failed'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(tables, 0);
    }
}
