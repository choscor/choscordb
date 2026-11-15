use choscordb_core::{
    MAX_SQL_FILE_STATEMENT_BYTES, SqlFileMode, SqlFileSplitter, SqlFileStatement,
};

fn statement(line: u64, sql: &str) -> SqlFileStatement {
    SqlFileStatement {
        line,
        sql: sql.into(),
    }
}

#[test]
fn chunks_split_anywhere_give_whole_statements_with_their_lines() {
    let file =
        "\u{feff}-- dump\nCREATE TABLE t (a text);\nINSERT INTO t VALUES ('é;\n');\nSELECT 1";
    let bytes = file.as_bytes();
    for cut in 1..bytes.len() {
        let mut splitter = SqlFileSplitter::new(None);
        let mut statements = splitter.push(&bytes[..cut], false).unwrap();
        statements.extend(splitter.push(&bytes[cut..], true).unwrap());
        assert_eq!(
            statements,
            [
                statement(2, "CREATE TABLE t (a text)"),
                statement(3, "INSERT INTO t VALUES ('é;\n')"),
                statement(5, "SELECT 1"),
            ],
            "cut at byte {cut}"
        );
    }
}

#[test]
fn mysql_dumps_keep_the_delimiter_across_chunks() {
    let file = "DELIMITER ;;\nCREATE TRIGGER x BEFORE INSERT ON t FOR EACH ROW BEGIN SET NEW.a = 1; END ;;\nDELIMITER ;\nSELECT 2;\n";
    let mut splitter = SqlFileSplitter::new(Some(Default::default()));
    let mut statements = splitter.push(&file.as_bytes()[..40], false).unwrap();
    statements.extend(splitter.push(&file.as_bytes()[40..], true).unwrap());
    assert_eq!(
        statements,
        [
            statement(
                2,
                "CREATE TRIGGER x BEFORE INSERT ON t FOR EACH ROW BEGIN SET NEW.a = 1; END"
            ),
            statement(4, "SELECT 2"),
        ]
    );
}

#[test]
fn bad_text_and_huge_statements_fail_with_a_line() {
    let mut splitter = SqlFileSplitter::new(None);
    let error = splitter
        .push(b"SELECT 1;\nSELECT '\xff';", true)
        .unwrap_err();
    assert_eq!(error.line, 2);
    assert_eq!(error.message, "The file is not UTF-8 text.");
    let mut splitter = SqlFileSplitter::new(None);
    let error = splitter
        .push(&vec![b'x'; MAX_SQL_FILE_STATEMENT_BYTES + 1], false)
        .unwrap_err();
    assert_eq!(error.line, 1);
    assert_eq!(
        error.message,
        format!(
            "A statement is larger than {} MiB.",
            MAX_SQL_FILE_STATEMENT_BYTES >> 20
        )
    );
}

#[test]
fn run_modes_have_codes_and_labels() {
    let codes: Vec<_> = SqlFileMode::ALL.iter().map(|mode| mode.code()).collect();
    assert_eq!(codes, ["stop", "continue", "transaction"]);
    assert_eq!(
        SqlFileMode::from_code("transaction"),
        Some(SqlFileMode::Transaction)
    );
    assert_eq!(SqlFileMode::from_code("x"), None);
    assert_eq!(SqlFileMode::StopOnError.label(), "Stop at the first error");
    use choscordb_core::export_machine::{ExportState, present_sql_file};
    assert_eq!(
        present_sql_file(&ExportState::Running, "").message,
        "Running the SQL file…"
    );
}

fn wait(
    engine: &mut choscordb_core::Engine,
    predicate: impl Fn(&choscordb_core::Event) -> bool,
) -> choscordb_core::Event {
    let until = std::time::Instant::now() + std::time::Duration::from_secs(10);
    loop {
        if let Some(event) = engine.try_event()
            && predicate(&event)
        {
            return event;
        }
        assert!(std::time::Instant::now() < until, "event timed out");
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
}

fn count(path: &std::path::Path) -> i64 {
    rusqlite::Connection::open(path)
        .unwrap()
        .query_row("SELECT count(*) FROM t", [], |row| row.get(0))
        .unwrap()
}

#[test]
fn the_engine_runs_a_file_in_each_mode() {
    use choscordb_core::{Engine, Event, SqlFileRun};
    let dir = tempfile::tempdir().unwrap();
    let database = dir.path().join("db.sqlite");
    rusqlite::Connection::open(&database)
        .unwrap()
        .execute_batch("CREATE TABLE t (a INTEGER UNIQUE);")
        .unwrap();
    let file = dir.path().join("load.sql");
    std::fs::write(
        &file,
        "INSERT INTO t VALUES (1);\nINSERT INTO t VALUES (1);\nINSERT INTO t VALUES (2);\n",
    )
    .unwrap();
    let mut engine = Engine::new(
        Default::default(),
        vec![std::sync::Arc::new(choscordb_driver_sqlite::SqliteDriver)],
    )
    .unwrap();
    let connection = engine
        .connect(
            "sqlite",
            choscordb_driver_api::ConnectionOptions::Sqlite {
                path: database.clone(),
                read_only: false,
            },
        )
        .unwrap();
    wait(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    let finished = |event: &Event| {
        matches!(
            event,
            Event::SqlFileFinished { .. } | Event::SqlFileFailed { .. }
        )
    };
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
        wait(engine, finished)
    };

    let event = run(&mut engine, SqlFileMode::Transaction);
    assert!(
        matches!(&event, Event::SqlFileFailed { statements: 1, error, .. }
            if error.message.starts_with("Line 2: ")
                && error.message.ends_with("The transaction was rolled back.")),
        "{event:?}"
    );
    assert_eq!(count(&database), 0);

    let event = run(&mut engine, SqlFileMode::StopOnError);
    assert!(
        matches!(&event, Event::SqlFileFailed { statements: 1, error, .. }
            if error.message.starts_with("Line 2: ")),
        "{event:?}"
    );
    assert_eq!(count(&database), 1);

    let event = run(&mut engine, SqlFileMode::ContinueOnError);
    let Event::SqlFileFinished {
        statements: 1,
        failed: 2,
        errors,
        ..
    } = event
    else {
        panic!("{event:?}");
    };
    assert!(errors[0].starts_with("Line 1: "), "{errors:?}");
    assert!(errors[1].starts_with("Line 2: "), "{errors:?}");
    assert_eq!(count(&database), 2);

    std::fs::remove_file(&file).unwrap();
    let event = run(&mut engine, SqlFileMode::StopOnError);
    assert!(
        matches!(&event, Event::SqlFileFailed { statements: 0, error, .. }
            if error.message.starts_with("The SQL file cannot be read: ")),
        "{event:?}"
    );
}
