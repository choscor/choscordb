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
fn preview_reads_only_the_rows_it_shows() {
    let dir = tempfile::tempdir().unwrap();
    let mut body = String::from("[");
    for row in 0..150 {
        body.push_str(&format!("{{\"n\": {row}}},"));
    }
    body.push_str("not json");
    let path = write(dir.path(), "broken.json", body.as_bytes());
    let file = json(path);
    assert_eq!(
        read_import_file(&file, ImportRead::Preview)
            .unwrap()
            .rows
            .len(),
        100
    );
    let error = read_import_file(&file, ImportRead::All).err().unwrap();
    assert_eq!(error.failed_statement, Some(150));
    assert!(error.message.starts_with("The JSON file is not valid"));
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

fn statements(plan: &choscordb_core::PlannedImport) -> Vec<choscordb_driver_api::EditStatement> {
    data()
        .rows
        .iter()
        .map(|row| plan.statement(row).unwrap())
        .collect()
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
        &data().columns,
    )
    .unwrap();
    assert_eq!(plan.create, None);
    let statements = statements(&plan);
    assert_eq!(statements.len(), 2);
    assert_eq!(
        statements[0].sql,
        "INSERT INTO \"main\".\"people\" (\"id\", \"name\") VALUES (?, ?)"
    );
    assert!(matches!(
        statements[1].params.as_slice(),
        [Value::Text(id), Value::Null] if id == "8"
    ));
}

#[test]
fn plan_rejects_bad_mappings_with_a_reason() {
    let message = |mapping: &[(&str, usize)]| {
        plan_table_import(
            &request(existing(), mapping),
            Some(&target()),
            &data().columns,
        )
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
        plan_table_import(
            &request(existing(), &[("id", 0)]),
            Some(&locked),
            &data().columns
        )
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
        &data().columns,
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
    assert_eq!(statements(&plan).len(), 2);
    assert_eq!(
        plan_table_import(
            &request(
                TableImportTarget::New {
                    qualified_name: "\"imported\"".into()
                },
                &[(" ", 0)]
            ),
            None,
            &data().columns
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
            wait(&mut self.engine, |event| {
                matches!(
                    event,
                    Event::TableImportFinished { import: id, .. }
                        | Event::TableImportFailed { import: id, .. } if *id == import
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
    fn progress_counts_written_rows_against_the_rows_in_the_file() {
        let mut fixture = Fixture::new();
        let path = write(
            fixture.dir.path(),
            "rows.csv",
            b"id,name\n2,Ann\n3,Bo\n4,Cy\n",
        );
        let mut request = request(existing(), &[("id", 0), ("name", 1)]);
        request.file = csv(path, CsvReadOptions::default());
        fixture
            .engine
            .import_table(fixture.connection, request)
            .unwrap();
        let mut progress = Vec::new();
        loop {
            match wait(&mut fixture.engine, |event| {
                matches!(
                    event,
                    Event::TableImportProgress { .. }
                        | Event::TableImportFinished { .. }
                        | Event::TableImportFailed { .. }
                )
            }) {
                Event::TableImportProgress { rows, total, .. } => progress.push((rows, total)),
                event => {
                    assert!(
                        matches!(event, Event::TableImportFinished { rows: 3, .. }),
                        "{event:?}"
                    );
                    break;
                }
            }
        }
        assert_eq!(progress.first(), Some(&(0, 3)));
        assert_eq!(progress.last(), Some(&(3, 3)));
    }

    fn numbered_rows(count: usize, bad: Option<(usize, &str)>) -> String {
        let mut body = String::from("id,name\n");
        for row in 1..=count {
            match bad {
                Some((at, line)) if at == row => body.push_str(line),
                _ => body.push_str(&format!("{},n{row}", row + 1)),
            }
            body.push('\n');
        }
        body
    }

    #[test]
    fn an_import_larger_than_one_chunk_writes_every_row() {
        let mut fixture = Fixture::new();
        let event = fixture.import(
            &numbered_rows(1_250, None),
            existing(),
            ImportConflict::Stop,
        );
        assert!(
            matches!(event, Event::TableImportFinished { rows: 1_250, .. }),
            "{event:?}"
        );
        let people = fixture.people();
        assert_eq!(people.len(), 1_251);
        assert_eq!(people.last(), Some(&(1_251, "n1250".into())));
    }

    #[test]
    fn a_failed_row_in_a_later_chunk_rolls_back_the_earlier_chunks() {
        let mut fixture = Fixture::new();
        let event = fixture.import(
            &numbered_rows(1_250, Some((1_100, "9999,"))),
            existing(),
            ImportConflict::Stop,
        );
        assert!(
            matches!(&event, Event::TableImportFailed { row: Some(1_100), error, .. } if error.message.contains("NOT NULL")),
            "{event:?}"
        );
        assert_eq!(fixture.people(), vec![(1, "Old".into())]);
    }

    #[test]
    fn a_bad_file_row_in_a_later_chunk_rolls_back_the_earlier_chunks() {
        let mut fixture = Fixture::new();
        let event = fixture.import(
            &numbered_rows(1_250, Some((1_100, "9999"))),
            existing(),
            ImportConflict::Stop,
        );
        assert!(
            matches!(&event, Event::TableImportFailed { row: Some(1_100), error, .. } if error.message.starts_with("Row 1100 has 1 fields")),
            "{event:?}"
        );
        assert_eq!(fixture.people(), vec![(1, "Old".into())]);
    }

    #[test]
    fn a_new_table_is_removed_when_a_later_chunk_fails() {
        let mut fixture = Fixture::new();
        let event = fixture.import(
            &numbered_rows(1_250, Some((1_100, "9999"))),
            TableImportTarget::New {
                qualified_name: "\"main\".\"later\"".into(),
            },
            ImportConflict::Stop,
        );
        assert!(
            matches!(
                event,
                Event::TableImportFailed {
                    row: Some(1_100),
                    ..
                }
            ),
            "{event:?}"
        );
        assert_eq!(
            fixture.rows("SELECT count(*), '' FROM sqlite_master WHERE name = 'later'"),
            vec![(0, String::new())]
        );
    }

    #[test]
    fn json_lines_larger_than_one_chunk_write_every_row() {
        let mut fixture = Fixture::new();
        let mut body = String::new();
        for row in 1..=1_250 {
            body.push_str(&format!("{{\"id\": {}, \"name\": \"n{row}\"}}\n", row + 1));
        }
        let path = write(fixture.dir.path(), "rows.jsonl", body.as_bytes());
        let mut request = request(existing(), &[("id", 0), ("name", 1)]);
        request.file = json(path);
        let import = fixture
            .engine
            .import_table(fixture.connection, request)
            .unwrap();
        let event = wait(&mut fixture.engine, |event| {
            matches!(
                event,
                Event::TableImportFinished { import: id, .. }
                    | Event::TableImportFailed { import: id, .. } if *id == import
            )
        });
        assert!(
            matches!(event, Event::TableImportFinished { rows: 1_250, .. }),
            "{event:?}"
        );
        assert_eq!(fixture.people().len(), 1_251);
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

    #[derive(Clone)]
    enum Gate {
        HoldSecondChunk(Arc<tokio::sync::Notify>),
        ConsumeInlineWithAFullChannel,
    }

    struct GatedSqlite {
        gate: Gate,
    }

    struct GatedConnection {
        inner: Box<dyn choscordb_driver_api::Connection>,
        gate: Gate,
    }

    #[async_trait::async_trait]
    impl choscordb_driver_api::DatabaseDriver for GatedSqlite {
        fn id(&self) -> &'static str {
            "sqlite"
        }
        fn capabilities(&self) -> choscordb_driver_api::DriverCapabilities {
            SqliteDriver.capabilities()
        }
        async fn connect(
            &self,
            options: ConnectionOptions,
        ) -> choscordb_driver_api::Result<Box<dyn choscordb_driver_api::Connection>> {
            Ok(Box::new(GatedConnection {
                inner: SqliteDriver.connect(options).await?,
                gate: self.gate.clone(),
            }))
        }
    }

    #[async_trait::async_trait]
    impl choscordb_driver_api::Connection for GatedConnection {
        async fn transaction_state(&mut self) -> choscordb_driver_api::Result<Option<bool>> {
            self.inner.transaction_state().await
        }
        async fn idle_transaction_state(
            &mut self,
        ) -> choscordb_driver_api::Result<Option<choscordb_driver_api::IdleTransactionState>>
        {
            self.inner.idle_transaction_state().await
        }
        fn cancellation_handle(&self) -> Arc<dyn choscordb_driver_api::CancelHandle> {
            self.inner.cancellation_handle()
        }
        async fn inspect_edit_target(
            &mut self,
            object: &ObjectId,
        ) -> choscordb_driver_api::Result<EditTarget> {
            self.inner.inspect_edit_target(object).await
        }
        async fn apply_edit_stream(
            &mut self,
            mut stream: choscordb_driver_api::EditStream,
        ) -> choscordb_driver_api::Result<choscordb_driver_api::EditBatchSummary> {
            let release = match &self.gate {
                Gate::HoldSecondChunk(release) => release.clone(),
                Gate::ConsumeInlineWithAFullChannel => {
                    let mut rows = stream.next_rows().await?.map_or(0, |rows| rows.len());
                    while stream.chunks.len() < 2 {
                        tokio::time::sleep(Duration::from_millis(1)).await;
                    }
                    tokio::time::sleep(Duration::from_millis(50)).await;
                    while let Some(chunk) = stream.next_rows().await? {
                        rows += chunk.len();
                    }
                    return Ok(choscordb_driver_api::EditBatchSummary {
                        affected_rows: vec![1; rows],
                    });
                }
            };
            let (sender, chunks) = tokio::sync::mpsc::channel(1);
            let mut source = stream.chunks;
            tokio::spawn(async move {
                let mut forwarded = 0;
                while let Some(chunk) = source.recv().await {
                    if forwarded == 1 {
                        release.notified().await;
                    }
                    forwarded += 1;
                    if sender.send(chunk).await.is_err() {
                        break;
                    }
                }
            });
            self.inner
                .apply_edit_stream(choscordb_driver_api::EditStream {
                    chunks,
                    progress: stream.progress,
                    cancel: stream.cancel,
                })
                .await
        }
        async fn execute_bounded(
            &mut self,
            sql: &str,
            options: choscordb_driver_api::QueryOptions,
            max_schema_bytes: usize,
        ) -> choscordb_driver_api::Result<Box<dyn choscordb_driver_api::ResultCursor>> {
            self.inner
                .execute_bounded(sql, options, max_schema_bytes)
                .await
        }
        async fn load_metadata(
            &mut self,
            parent: Option<ObjectId>,
        ) -> choscordb_driver_api::Result<Vec<choscordb_driver_api::SchemaObject>> {
            self.inner.load_metadata(parent).await
        }
        async fn commit(&mut self) -> choscordb_driver_api::Result<()> {
            self.inner.commit().await
        }
        async fn rollback(&mut self) -> choscordb_driver_api::Result<()> {
            self.inner.rollback().await
        }
        async fn close(&mut self) -> choscordb_driver_api::Result<()> {
            self.inner.close().await
        }
    }

    fn gated() -> (
        Fixture,
        Engine,
        choscordb_driver_api::Handle,
        Arc<tokio::sync::Notify>,
    ) {
        let release = Arc::new(tokio::sync::Notify::new());
        let (fixture, engine, connection) = gated_by(Gate::HoldSecondChunk(release.clone()));
        (fixture, engine, connection, release)
    }

    fn gated_by(gate: Gate) -> (Fixture, Engine, choscordb_driver_api::Handle) {
        let fixture = Fixture::new();
        let mut engine =
            Engine::new(Default::default(), vec![Arc::new(GatedSqlite { gate })]).unwrap();
        let connection = engine
            .connect(
                "sqlite",
                ConnectionOptions::Sqlite {
                    path: fixture.dir.path().join("db.sqlite"),
                    read_only: false,
                },
            )
            .unwrap();
        wait(&mut engine, |event| {
            matches!(event, Event::Connected { .. })
        });
        (fixture, engine, connection)
    }

    fn start(
        fixture: &Fixture,
        engine: &mut Engine,
        connection: choscordb_driver_api::Handle,
    ) -> choscordb_core::ExportId {
        let path = write(
            fixture.dir.path(),
            "rows.csv",
            numbered_rows(600, None).as_bytes(),
        );
        let mut request = request(existing(), &[("id", 0), ("name", 1)]);
        request.file = csv(path, CsvReadOptions::default());
        let import = engine.import_table(connection, request).unwrap();
        let event = wait(engine, |event| {
            matches!(
                event,
                Event::TableImportProgress { rows: 500, .. }
                    | Event::TableImportFinished { .. }
                    | Event::TableImportFailed { .. }
            )
        });
        assert!(
            matches!(
                event,
                Event::TableImportProgress {
                    rows: 500,
                    total: 600,
                    ..
                }
            ),
            "{event:?}"
        );
        import
    }

    fn outcome(engine: &mut Engine) -> Event {
        wait(engine, |event| {
            matches!(
                event,
                Event::TableImportFinished { .. } | Event::TableImportFailed { .. }
            )
        })
    }

    #[test]
    fn progress_is_reported_while_the_rows_are_still_being_written() {
        let (fixture, mut engine, connection, release) = gated();
        start(&fixture, &mut engine, connection);
        release.notify_one();
        let event = outcome(&mut engine);
        assert!(
            matches!(event, Event::TableImportFinished { rows: 600, .. }),
            "{event:?}"
        );
        assert_eq!(fixture.people().len(), 601);
    }

    #[test]
    fn cancel_during_the_write_rolls_back_the_written_chunks() {
        let (fixture, mut engine, connection, release) = gated();
        let import = start(&fixture, &mut engine, connection);
        engine.cancel_export(import).unwrap();
        release.notify_one();
        let event = outcome(&mut engine);
        assert!(
            matches!(&event, Event::TableImportFailed { error, .. } if error.kind == ErrorKind::Cancelled),
            "{event:?}"
        );
        assert_eq!(fixture.people(), vec![(1, "Old".into())]);
    }

    #[test]
    fn a_driver_that_consumes_inline_finishes_when_the_channel_is_full() {
        let (fixture, mut engine, connection) = gated_by(Gate::ConsumeInlineWithAFullChannel);
        let path = write(
            fixture.dir.path(),
            "rows.csv",
            numbered_rows(1_250, None).as_bytes(),
        );
        let mut request = request(existing(), &[("id", 0), ("name", 1)]);
        request.file = csv(path, CsvReadOptions::default());
        engine.import_table(connection, request).unwrap();
        let event = outcome(&mut engine);
        assert!(
            matches!(event, Event::TableImportFinished { rows: 1_250, .. }),
            "{event:?}"
        );
    }
}
