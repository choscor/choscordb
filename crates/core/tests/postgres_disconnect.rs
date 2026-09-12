//! Run against the repository's disposable PostgreSQL fixture, never production.
use choscordb_core::{Engine, Event, HistoryStatus, QueryState};
use choscordb_driver_api::{
    Connection, ConnectionOptions, DatabaseDriver, PageSize, QueryOptions, Row, Secret, TlsMode,
    Value,
};
use choscordb_driver_postgres::PostgresDriver;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};

fn options() -> ConnectionOptions {
    let get =
        |name| std::env::var(name).expect("repository PostgreSQL fixture environment required");
    ConnectionOptions::Postgres {
        host: get("CHOSCORDB_TEST_POSTGRES_HOST"),
        port: get("CHOSCORDB_TEST_POSTGRES_PORT").parse().unwrap(),
        database: get("CHOSCORDB_TEST_POSTGRES_DATABASE"),
        user: get("CHOSCORDB_TEST_POSTGRES_USER"),
        password: Some(Secret::new(get("CHOSCORDB_TEST_POSTGRES_PASSWORD"))),
        tls: TlsMode::VerifyFull,
        root_certificate: Some(get("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE").into()),
    }
}
fn until(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) -> Event {
    let deadline = Instant::now() + Duration::from_secs(15);
    loop {
        if let Some(event) = engine.try_event() {
            if predicate(&event) {
                return event;
            }
            assert!(
                !matches!(event, Event::ConnectionFailed { .. }),
                "fixture connection failed"
            );
        }
        assert!(
            Instant::now() < deadline,
            "timed out waiting for engine event"
        );
        std::thread::sleep(Duration::from_millis(1));
    }
}
async fn sql(connection: &mut dyn Connection, sql: &str) -> Vec<Row> {
    let mut cursor = connection
        .execute(sql, QueryOptions::default())
        .await
        .unwrap();
    let mut rows = Vec::new();
    loop {
        let page = cursor
            .fetch_page(PageSize::new(100).unwrap())
            .await
            .unwrap();
        rows.extend(page.rows);
        if !page.has_more {
            break;
        }
    }
    cursor.close().await.unwrap();
    rows
}
#[test]
#[ignore = "requires exclusive disposable PostgreSQL fixture environment"]
fn disconnect_interrupts_blocked_implicit_command() {
    let runtime = tokio::runtime::Runtime::new().unwrap();
    let mut blocker = runtime.block_on(PostgresDriver.connect(options())).unwrap();
    let table = format!("choscordb_direct_close_{}", uuid::Uuid::new_v4().simple());
    runtime.block_on(sql(
        &mut *blocker,
        &format!("CREATE TABLE {table}(id integer)"),
    ));
    let mut lock = runtime
        .block_on(blocker.execute(
            &format!("LOCK TABLE {table} IN ACCESS EXCLUSIVE MODE"),
            QueryOptions {
                auto_commit: false,
                ..Default::default()
            },
        ))
        .unwrap();
    runtime
        .block_on(lock.fetch_page(PageSize::default()))
        .unwrap();
    runtime.block_on(lock.close()).unwrap();
    drop(lock);

    let mut engine = Engine::new(Default::default(), vec![Arc::new(PostgresDriver)]).unwrap();
    let connection = engine.connect("postgres", options()).unwrap();
    until(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    let query = engine
        .execute(
            connection,
            format!("VACUUM {table}"),
            QueryOptions::default(),
        )
        .unwrap();
    until(
        &mut engine,
        |event| matches!(event, Event::QueryState { query: id, state: QueryState::Running } if *id == query),
    );
    std::thread::sleep(Duration::from_millis(50));
    let started = Instant::now();
    engine.disconnect(connection).unwrap();
    until(
        &mut engine,
        |event| matches!(event, Event::Disconnected { connection: id } if *id == connection),
    );
    assert!(started.elapsed() < Duration::from_secs(3));

    runtime.block_on(blocker.rollback()).unwrap();
    runtime.block_on(sql(&mut *blocker, &format!("DROP TABLE {table}")));
    runtime.block_on(blocker.close()).unwrap();
}
#[test]
#[ignore = "requires disposable PostgreSQL fixture environment"]
fn suspended_returning_disconnect_rolls_back_before_cursor_finalization() {
    let runtime = tokio::runtime::Runtime::new().unwrap();
    let mut setup = runtime.block_on(PostgresDriver.connect(options())).unwrap();
    let table = format!("choscordb_disconnect_{}", uuid::Uuid::new_v4().simple());
    runtime.block_on(sql(
        &mut *setup,
        &format!("CREATE TABLE {table} (id integer PRIMARY KEY, value integer NOT NULL)"),
    ));
    runtime.block_on(sql(
        &mut *setup,
        &format!("INSERT INTO {table} SELECT i, 0 FROM generate_series(1, 300) i"),
    ));
    let mut observed = Vec::new();
    for update in [false, true] {
        for fetch in [false, true] {
            let mut engine =
                Engine::new(Default::default(), vec![Arc::new(PostgresDriver)]).unwrap();
            let connection = engine.connect("postgres", options()).unwrap();
            until(&mut engine, |e| matches!(e, Event::Connected { .. }));
            let statement = if update {
                format!("UPDATE {table} SET value = 1 RETURNING id")
            } else {
                format!(
                    "INSERT INTO {table} SELECT i, 1 FROM generate_series(1001, 1300) i RETURNING id"
                )
            };
            let query = engine
                .execute(connection, statement, QueryOptions::default())
                .unwrap();
            until(&mut engine, |e| matches!(e, Event::Schema { .. }));
            if fetch {
                engine
                    .fetch_page(query, PageSize::new(100).unwrap())
                    .unwrap();
                let event = until(&mut engine, |e| matches!(e, Event::Page { .. }));
                if let Event::Page { page, .. } = &event {
                    assert!(page.has_more, "regression requires a suspended portal");
                    assert_eq!(page.rows.len(), 100);
                }
                drop(event);
            }
            engine.disconnect(connection).unwrap();
            until(&mut engine, |e| matches!(e, Event::Disconnected { .. }));
            engine.history_list(10, 0, 1).unwrap();
            let Event::HistoryListed { entries, .. } =
                until(&mut engine, |e| matches!(e, Event::HistoryListed { .. }))
            else {
                unreachable!()
            };
            assert_eq!(entries.len(), 1);
            assert_eq!(entries[0].status, HistoryStatus::Disconnected);
            // A fresh connection proves actual committed database state, not actor bookkeeping.
            let mut reader = runtime.block_on(PostgresDriver.connect(options())).unwrap();
            let rows = runtime.block_on(sql(
                &mut *reader,
                &format!("SELECT count(*)::bigint, sum(value)::bigint FROM {table}"),
            ));
            observed.push(rows);
            runtime.block_on(reader.close()).unwrap();
            // Reset fixture rows so every case is independent even on the red implementation.
            runtime.block_on(sql(
                &mut *setup,
                &format!("DELETE FROM {table} WHERE id > 300"),
            ));
            runtime.block_on(sql(&mut *setup, &format!("UPDATE {table} SET value = 0")));
        }
    }
    runtime.block_on(sql(&mut *setup, &format!("DROP TABLE {table}")));
    runtime.block_on(setup.close()).unwrap();
    for rows in observed {
        assert_eq!(
            rows,
            vec![vec![Value::Integer(300), Value::Integer(0)]],
            "disconnect committed suspended RETURNING writes"
        );
    }
}
