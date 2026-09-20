//! Run only against the repository's disposable MySQL fixture.
use choscordb_core::{ConnectionProfile, Engine, Event, PostgresTls, ProfileConfiguration};
use choscordb_driver_api::{PageSize, QueryOptions, Secret, TlsMode, Value};
use choscordb_driver_mysql::MysqlDriver;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};

fn until(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) -> Event {
    let deadline = Instant::now() + Duration::from_secs(15);
    loop {
        if let Some(event) = engine.try_event() {
            if predicate(&event) {
                return event;
            }
            match &event {
                Event::ConnectionFailed { error, .. } | Event::QueryFailed { error, .. } => {
                    panic!("MySQL fixture operation failed: {error}")
                }
                _ => {}
            }
        }
        assert!(
            Instant::now() < deadline,
            "timed out waiting for MySQL engine event"
        );
        std::thread::sleep(Duration::from_millis(1));
    }
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn mysql_profile_connects_streams_typed_rows_and_disconnects_through_engine() {
    let mut engine = Engine::new(Default::default(), vec![Arc::new(MysqlDriver::new())]).unwrap();
    let connection = engine
        .connect_profile(
            ConnectionProfile {
                id: "mysql-fixture".into(),
                name: "MySQL fixture".into(),
                group_id: None,
                credential_ref: None,
                configuration: ProfileConfiguration::Mysql {
                    ssh: None,
                    host: "127.0.0.1".into(),
                    port: std::env::var("CHOSCORDB_MYSQL_PORT")
                        .unwrap_or_else(|_| "33306".into())
                        .parse()
                        .unwrap(),
                    database: "choscordb_test".into(),
                    user: "root".into(),
                    tls: PostgresTls {
                        mode: TlsMode::Disable,
                        root_certificate_path: None,
                    },
                },
            },
            Some(Secret::new("choscordb-test-password")),
        )
        .unwrap();
    until(
        &mut engine,
        |event| matches!(event, Event::Connected { connection: id, .. } if *id == connection),
    );
    let query = engine.execute(connection,
        "SELECT 42 AS answer, 'hello' AS greeting, NULL AS absent UNION ALL SELECT 7, 'goodbye', NULL".into(),
        QueryOptions::default()).unwrap();
    let Event::Schema { columns, .. } = until(
        &mut engine,
        |event| matches!(event, Event::Schema { query: id, .. } if *id == query),
    ) else {
        unreachable!()
    };
    assert_eq!(
        columns
            .iter()
            .map(|column| column.name.as_str())
            .collect::<Vec<_>>(),
        ["answer", "greeting", "absent"]
    );
    engine
        .fetch_page(query, PageSize::new(100).unwrap())
        .unwrap();
    let Event::Page { page, .. } = until(
        &mut engine,
        |event| matches!(event, Event::Page { query: id, .. } if *id == query),
    ) else {
        unreachable!()
    };
    assert_eq!(
        page.rows,
        vec![
            vec![Value::Integer(42), Value::Text("hello".into()), Value::Null],
            vec![
                Value::Integer(7),
                Value::Text("goodbye".into()),
                Value::Null
            ]
        ]
    );
    assert!(!page.has_more);
    engine.disconnect(connection).unwrap();
    loop {
        let event = until(&mut engine, |event| {
            matches!(event,
            Event::Disconnected { connection: id } if *id == connection)
                || matches!(event, Event::QueryFailed { query: id, error } if *id == query && error.kind == choscordb_driver_api::ErrorKind::Disconnected)
        });
        if matches!(event, Event::Disconnected { .. }) {
            break;
        }
    }
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn cancelling_slow_mysql_object_is_prompt_and_preserves_main_session() {
    use choscordb_core::QueryState;
    use choscordb_driver_api::{ConnectionOptions, DatabaseDriver, ErrorKind, ObjectId};
    let runtime = tokio::runtime::Runtime::new().unwrap();
    let options = || ConnectionOptions::Mysql {
        ssh: None,
        host: "127.0.0.1".into(),
        port: std::env::var("CHOSCORDB_MYSQL_PORT")
            .unwrap_or_else(|_| "33306".into())
            .parse()
            .unwrap(),
        database: "choscordb_test".into(),
        user: "root".into(),
        password: Some(Secret::new("choscordb-test-password")),
        tls: TlsMode::Disable,
        root_certificate: None,
    };
    let view = format!("choscordb_slow_{}", uuid::Uuid::new_v4().simple());
    let mut setup = runtime
        .block_on(MysqlDriver::new().connect(options()))
        .unwrap();
    runtime
        .block_on(setup.execute(
            &format!("CREATE VIEW `{view}` AS SELECT SLEEP(30) AS sleeping_value"),
            QueryOptions::default(),
        ))
        .unwrap();
    let mut engine = Engine::new(Default::default(), vec![Arc::new(MysqlDriver::new())]).unwrap();
    let connection = engine.connect("mysql", options()).unwrap();
    until(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    let query = engine
        .open_object_data(
            connection,
            ObjectId(format!(r#"["choscordb_test","{view}"]"#)),
            QueryOptions::default(),
        )
        .unwrap();
    until(
        &mut engine,
        |event| matches!(event, Event::QueryState { query: id, state: QueryState::Running } if *id == query),
    );
    engine
        .fetch_page(query, PageSize::new(100).unwrap())
        .unwrap();
    std::thread::sleep(Duration::from_millis(100));
    let started = Instant::now();
    engine.cancel(query).unwrap();
    loop {
        if let Some(Event::QueryFailed { query: id, error }) = engine.try_event() {
            assert_eq!(id, query);
            assert_eq!(error.kind, ErrorKind::Cancelled);
            break;
        }
        assert!(
            started.elapsed() < Duration::from_secs(3),
            "MySQL object cancellation did not settle promptly"
        );
        std::thread::sleep(Duration::from_millis(1));
    }
    let query = engine
        .execute(connection, "SELECT 29".into(), QueryOptions::default())
        .unwrap();
    until(
        &mut engine,
        |event| matches!(event, Event::Schema { query: id, .. } if *id == query),
    );
    engine
        .fetch_page(query, PageSize::new(100).unwrap())
        .unwrap();
    let Event::Page { page, .. } = until(
        &mut engine,
        |event| matches!(event, Event::Page { query: id, .. } if *id == query),
    ) else {
        unreachable!()
    };
    assert_eq!(page.rows, vec![vec![Value::Integer(29)]]);
    engine.disconnect(connection).unwrap();
    loop {
        let event = until(&mut engine, |event| {
            matches!(event,
            Event::Disconnected { connection: id } if *id == connection)
                || matches!(event, Event::QueryFailed { query: id, error } if *id == query && error.kind == choscordb_driver_api::ErrorKind::Disconnected)
        });
        if matches!(event, Event::Disconnected { .. }) {
            break;
        }
    }
    runtime
        .block_on(setup.execute(&format!("DROP VIEW `{view}`"), QueryOptions::default()))
        .unwrap();
    runtime.block_on(setup.close()).unwrap();
}

fn direct_engine() -> (Engine, choscordb_driver_api::ConnectionId) {
    let mut engine = Engine::new(Default::default(), vec![Arc::new(MysqlDriver::new())]).unwrap();
    let connection = engine
        .connect(
            "mysql",
            choscordb_driver_api::ConnectionOptions::Mysql {
                host: "127.0.0.1".into(),
                port: 33306,
                database: "choscordb_test".into(),
                user: "root".into(),
                password: Some(Secret::new("choscordb-test-password")),
                tls: TlsMode::Disable,
                root_certificate: None,
                ssh: None,
            },
        )
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::Connected { .. }));
    (engine, connection)
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn mysql_script_results_advance_with_new_schema_and_pages() {
    let (mut engine, connection) = direct_engine();
    let query = engine
        .execute(
            connection,
            "SELECT 11 AS first_answer; SELECT 'second' AS second_answer".into(),
            QueryOptions {
                timeout: Some(Duration::from_secs(1)),
                ..Default::default()
            },
        )
        .unwrap();
    let Event::Schema { columns, .. } = until(&mut engine, |e| matches!(e, Event::Schema { .. }))
    else {
        unreachable!()
    };
    assert_eq!(columns[0].name, "first_answer");
    engine
        .fetch_page_at(query, 0, PageSize::new(100).unwrap())
        .unwrap();
    let Event::StoredPage { page, .. } =
        until(&mut engine, |e| matches!(e, Event::StoredPage { .. }))
    else {
        unreachable!()
    };
    assert_eq!(page.rows, vec![vec![Value::Integer(11)]]);
    let Event::QueryFinished { summary, .. } =
        until(&mut engine, |e| matches!(e, Event::QueryFinished { .. }))
    else {
        unreachable!()
    };
    assert!(summary.has_more_results);
    std::thread::sleep(Duration::from_millis(1100));
    engine.next_result_set(query).unwrap();
    let Event::Schema { columns, .. } = until(&mut engine, |e| matches!(e, Event::Schema { .. }))
    else {
        unreachable!()
    };
    assert_eq!(columns[0].name, "second_answer");
    engine
        .fetch_page_at(query, 0, PageSize::new(100).unwrap())
        .unwrap();
    let Event::StoredPage { page, .. } =
        until(&mut engine, |e| matches!(e, Event::StoredPage { .. }))
    else {
        unreachable!()
    };
    assert_eq!(page.rows, vec![vec![Value::Text("second".into())]]);
}

#[test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
fn mysql_metadata_continuation_reaches_later_schemas() {
    let (mut engine, connection) = direct_engine();
    engine
        .load_metadata_page(connection, None, 801, 0, 1)
        .unwrap();
    let Event::Metadata {
        objects,
        offset,
        next_offset,
        request_token,
        ..
    } = until(&mut engine, |e| matches!(e, Event::Metadata { .. }))
    else {
        unreachable!()
    };
    assert_eq!(request_token, 801);
    assert_eq!(offset, 0);
    assert_eq!(objects.len(), 1);
    let first = objects[0].name.clone();
    let next = next_offset.expect("fixture has multiple schemas");
    engine
        .load_metadata_page(connection, None, 802, next, 1)
        .unwrap();
    let Event::Metadata {
        objects,
        offset,
        request_token,
        ..
    } = until(&mut engine, |e| matches!(e, Event::Metadata { .. }))
    else {
        unreachable!()
    };
    assert_eq!(request_token, 802);
    assert_eq!(offset, 1);
    assert_eq!(objects.len(), 1);
    assert_ne!(objects[0].name, first);
}
