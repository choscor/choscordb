//! MariaDB-only engine behavior. Run with CHOSCORDB_MARIADB_PORT set to the
//! repository's disposable MariaDB fixture.
use choscordb_core::{Engine, Event};
use choscordb_driver_api::{
    ConnectionId, ConnectionOptions, ErrorKind, HistoryTimestamp, ObjectDataTime, ObjectId,
    PageSize, QueryId, QueryOptions, TlsMode, Value,
};
use choscordb_driver_mysql::MysqlDriver;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};
#[path = "../../driver-mysql/tests/support/server.rs"]
mod server;

fn until(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) -> Event {
    let deadline = Instant::now() + Duration::from_secs(15);
    loop {
        if let Some(event) = engine.try_event() {
            if predicate(&event) {
                return event;
            }
            if let Event::ConnectionFailed { error, .. } | Event::QueryFailed { error, .. } = &event
            {
                panic!("MariaDB fixture operation failed: {error}")
            }
        }
        assert!(
            Instant::now() < deadline,
            "timed out waiting for MariaDB engine event"
        );
        std::thread::sleep(Duration::from_millis(1));
    }
}

fn engine() -> Engine {
    Engine::new(
        Default::default(),
        vec![Arc::new(MysqlDriver::new()), server::driver()],
    )
    .unwrap()
}

/// The MariaDB fixture's settings as a MySQL connection.
fn mysql_options() -> ConnectionOptions {
    let ConnectionOptions::Mariadb {
        proxy,
        proxy_secret,
        host,
        port,
        database,
        user,
        password,
        ssh_secret,
        ssh_jump_secrets,
        ssh_private_key,
        ssh_jump_private_keys,
        tls,
        root_certificate,
        tls_identity,
        ssh,
    } = server::options(TlsMode::Disable)
    else {
        panic!("the MariaDB tests need CHOSCORDB_MARIADB_PORT")
    };
    ConnectionOptions::Mysql {
        proxy,
        proxy_secret,
        host,
        port,
        database,
        user,
        password,
        ssh_secret,
        ssh_jump_secrets,
        ssh_private_key,
        ssh_jump_private_keys,
        tls,
        root_certificate,
        tls_identity,
        ssh,
    }
}

fn connected_notices(engine: &mut Engine, driver: &str, options: ConnectionOptions) -> Vec<String> {
    let connection = engine.connect(driver, options).unwrap();
    let Event::Connected { notices, .. } = until(
        engine,
        |event| matches!(event, Event::Connected { connection: id, .. } if *id == connection),
    ) else {
        unreachable!()
    };
    notices
}

#[test]
#[ignore = "requires disposable MariaDB server"]
fn a_mysql_connection_to_mariadb_connects_with_a_notice() {
    let mut engine = engine();
    let notices = connected_notices(&mut engine, "mysql", mysql_options());
    assert_eq!(
        notices,
        ["This server is MariaDB; use a MariaDB connection for MariaDB features"]
    );
    assert!(
        connected_notices(&mut engine, "mariadb", server::options(TlsMode::Disable)).is_empty()
    );
}

fn paged() -> QueryOptions {
    QueryOptions {
        page_size: PageSize::new(100).unwrap(),
        ..QueryOptions::default()
    }
}

fn setup(statements: &[&str]) -> Vec<Value> {
    let runtime = tokio::runtime::Runtime::new().unwrap();
    runtime.block_on(async {
        let mut conn = server::driver()
            .connect(server::options(TlsMode::Disable))
            .await
            .unwrap();
        let mut last = Vec::new();
        for sql in statements {
            let mut cursor = conn.execute(sql, paged()).await.unwrap();
            if !cursor.columns().is_empty() {
                last = cursor
                    .fetch_page(PageSize::default())
                    .await
                    .unwrap()
                    .rows
                    .remove(0);
            }
            cursor.close().await.unwrap();
            std::thread::sleep(Duration::from_millis(20));
        }
        conn.close().await.unwrap();
        last
    })
}

fn connected(engine: &mut Engine, driver: &str, options: ConnectionOptions) -> ConnectionId {
    let connection = engine.connect(driver, options).unwrap();
    until(
        engine,
        |event| matches!(event, Event::Connected { connection: id, .. } if *id == connection),
    );
    connection
}

/// The column names and rows of an object read.
fn object_rows(engine: &mut Engine, query: QueryId) -> (Vec<String>, Vec<Vec<Value>>) {
    let Event::Schema { columns, .. } = until(
        engine,
        |event| matches!(event, Event::Schema { query: id, .. } if *id == query),
    ) else {
        unreachable!()
    };
    engine.fetch_page_at(query, 0).unwrap();
    let Event::StoredPage { page, .. } = until(
        engine,
        |event| matches!(event, Event::StoredPage { query: id, .. } if *id == query),
    ) else {
        unreachable!()
    };
    (
        columns.into_iter().map(|column| column.name).collect(),
        page.rows,
    )
}

fn edit_reason(engine: &mut Engine, connection: ConnectionId, time: ObjectDataTime) -> String {
    engine
        .edit_target_request(connection, history_object(), time, 7)
        .unwrap();
    let Event::EditTarget { target, .. } = until(engine, |event| {
        matches!(
            event,
            Event::EditTarget {
                request_token: 7,
                ..
            }
        )
    }) else {
        unreachable!()
    };
    target.reason
}

fn history_object() -> ObjectId {
    ObjectId(r#"["choscordb_test","history_items"]"#.into())
}

#[test]
#[ignore = "requires disposable MariaDB server"]
fn history_reads_return_past_rows_with_period_columns_and_are_read_only() {
    let middle = setup(&[
        "DROP TABLE IF EXISTS history_items",
        "CREATE TABLE history_items (id INT PRIMARY KEY, label TEXT) WITH SYSTEM VERSIONING",
        "INSERT INTO history_items VALUES (1, 'first')",
        "SELECT CAST(NOW(6) AS CHAR)",
        "UPDATE history_items SET label = 'second' WHERE id = 1",
    ]);
    let [Value::Text(middle)] = middle.as_slice() else {
        panic!("NOW(6) is text")
    };
    let mut engine = engine();
    let connection = connected(&mut engine, "mariadb", server::options(TlsMode::Disable));

    let current = engine
        .open_object_data(
            connection,
            history_object(),
            ObjectDataTime::Current,
            paged(),
        )
        .unwrap();
    assert_eq!(
        object_rows(&mut engine, current),
        (
            vec!["id".into(), "label".into()],
            vec![vec![Value::Integer(1), Value::Text("second".into())]]
        )
    );
    assert_eq!(
        edit_reason(&mut engine, connection, ObjectDataTime::Current),
        ""
    );

    let as_of = ObjectDataTime::AsOf(HistoryTimestamp::parse(middle).unwrap());
    let past = engine
        .open_object_data(connection, history_object(), as_of.clone(), paged())
        .unwrap();
    let (columns, rows) = object_rows(&mut engine, past);
    assert_eq!(columns, ["id", "label", "ROW_START", "ROW_END"]);
    assert_eq!(rows.len(), 1);
    assert_eq!(
        rows[0][..2],
        [Value::Integer(1), Value::Text("first".into())]
    );
    assert!(matches!(rows[0][2], Value::Timestamp(_)));
    assert_eq!(
        edit_reason(&mut engine, connection, as_of),
        "History rows are read-only"
    );

    let all = engine
        .open_object_data(connection, history_object(), ObjectDataTime::All, paged())
        .unwrap();
    let (_, mut rows) = object_rows(&mut engine, all);
    rows.sort_by(|left, right| format!("{:?}", left[2]).cmp(&format!("{:?}", right[2])));
    assert_eq!(
        rows.iter().map(|row| row[1].clone()).collect::<Vec<_>>(),
        [Value::Text("first".into()), Value::Text("second".into())]
    );
    assert_eq!(
        edit_reason(&mut engine, connection, ObjectDataTime::All),
        "History rows are read-only"
    );
}

#[test]
#[ignore = "requires disposable MariaDB server"]
fn history_reads_are_unsupported_on_a_mysql_connection() {
    setup(&[
        "DROP TABLE IF EXISTS history_items",
        "CREATE TABLE history_items (id INT PRIMARY KEY, label TEXT) WITH SYSTEM VERSIONING",
    ]);
    let mut engine = engine();
    let connection = connected(&mut engine, "mysql", mysql_options());
    let query = engine
        .open_object_data(connection, history_object(), ObjectDataTime::All, paged())
        .unwrap();
    let Event::QueryFailed { error, .. } = until_failure(&mut engine, query) else {
        unreachable!()
    };
    assert_eq!(error.kind, ErrorKind::Unsupported);
}

fn until_failure(engine: &mut Engine, query: QueryId) -> Event {
    let deadline = Instant::now() + Duration::from_secs(15);
    loop {
        if let Some(event) = engine.try_event()
            && matches!(&event, Event::QueryFailed { query: id, .. } if *id == query)
        {
            return event;
        }
        assert!(Instant::now() < deadline, "the history read did not fail");
        std::thread::sleep(Duration::from_millis(1));
    }
}
