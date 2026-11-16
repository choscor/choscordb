use choscordb_core::{Engine, Event, structure_edit_unavailable};
use choscordb_driver_api::{
    ConnectionOptions, DatabaseDriver, StructureChange, StructureChangeKind, StructureColumn,
    StructureTable, plan_structure,
};
use choscordb_driver_sqlite::SqliteDriver;
use std::collections::BTreeMap;
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

fn change(kind: StructureChangeKind, values: &[(&str, &str)]) -> StructureChange {
    StructureChange {
        kind,
        values: values
            .iter()
            .map(|(key, value)| ((*key).to_owned(), vec![(*value).to_owned()]))
            .collect::<BTreeMap<_, _>>(),
    }
}

#[test]
fn only_plain_tables_have_an_editable_structure() {
    assert_eq!(structure_edit_unavailable("table", ""), None);
    assert_eq!(
        structure_edit_unavailable("view", ""),
        Some("Only tables have a structure to edit.")
    );
}

#[test]
fn planned_structure_changes_apply_in_order_and_name_the_failed_statement() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("db.sqlite");
    rusqlite::Connection::open(&path)
        .unwrap()
        .execute_batch(
            "CREATE TABLE people (id INTEGER PRIMARY KEY, name TEXT); \
             INSERT INTO people (name) VALUES ('a')",
        )
        .unwrap();
    let mut engine = Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let connection = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: path.clone(),
                read_only: false,
            },
        )
        .unwrap();
    wait(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    let table = StructureTable {
        qualified_name: "\"main\".\"people\"".into(),
        new_table: false,
        columns: vec![
            StructureColumn {
                name: "id".into(),
                data_type: "INTEGER".into(),
                nullable: false,
            },
            StructureColumn {
                name: "name".into(),
                data_type: "TEXT".into(),
                nullable: true,
            },
        ],
        indexes: vec![],
        keys: vec![],
    };
    let sqlite = SqliteDriver.structure_dialect().unwrap();
    assert_eq!(
        plan_structure(
            &table,
            sqlite,
            &[change(
                StructureChangeKind::PrimaryKey,
                &[("columns", "id")]
            )],
        )
        .unwrap_err()
        .to_string(),
        "Change 1: SQLite needs a table rebuild for this change."
    );
    let statements = plan_structure(
        &table,
        sqlite,
        &[
            change(
                StructureChangeKind::AddColumn,
                &[("name", "email"), ("type", "TEXT")],
            ),
            change(
                StructureChangeKind::AddIndex,
                &[("name", "people_email"), ("columns", "email")],
            ),
            change(
                StructureChangeKind::ChangeColumn,
                &[("column", "name"), ("name", "full_name")],
            ),
        ],
    )
    .unwrap();
    engine.apply_ddl(connection, statements, 7).unwrap();
    let event = wait(&mut engine, |event| {
        matches!(event, Event::EditApplied { .. } | Event::EditFailed { .. })
    });
    assert!(
        matches!(
            event,
            Event::EditApplied {
                request_token: 7,
                ..
            }
        ),
        "{event:?}"
    );
    let db = rusqlite::Connection::open(&path).unwrap();
    let columns: Vec<String> = db
        .prepare("SELECT name FROM pragma_table_info('people') ORDER BY cid")
        .unwrap()
        .query_map([], |row| row.get(0))
        .unwrap()
        .collect::<Result<_, _>>()
        .unwrap();
    assert_eq!(columns, ["id", "full_name", "email"]);
    let indexes: i64 = db
        .query_row(
            "SELECT count(*) FROM sqlite_master WHERE name = 'people_email'",
            [],
            |row| row.get(0),
        )
        .unwrap();
    assert_eq!(indexes, 1);
    drop(db);

    engine
        .apply_ddl(
            connection,
            vec![
                "ALTER TABLE people ADD COLUMN age INTEGER".into(),
                "ALTER TABLE people ADD COLUMN age INTEGER".into(),
            ],
            8,
        )
        .unwrap();
    let event = wait(&mut engine, |event| {
        matches!(event, Event::EditApplied { .. } | Event::EditFailed { .. })
    });
    assert!(
        matches!(&event, Event::EditFailed { request_token: 8, error, .. } if error.failed_statement == Some(1) && error.message.starts_with("Statement 2: ")),
        "{event:?}"
    );

    engine
        .apply_ddl(
            connection,
            vec!["ALTER TABLE people ADD COLUMN code INTEGER NOT NULL".into()],
            9,
        )
        .unwrap();
    let event = wait(&mut engine, |event| {
        matches!(event, Event::EditApplied { .. } | Event::EditFailed { .. })
    });
    assert!(
        matches!(&event, Event::EditFailed { request_token: 9, error, .. } if error.message.starts_with("Statement 1: ")),
        "{event:?}"
    );
}
