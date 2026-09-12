use choscordb_core::{Engine, Event};
use choscordb_driver_api::{ConnectionOptions, PageSize, QueryOptions};
use std::{
    sync::Arc,
    time::{Duration, Instant},
};
fn until(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(event) = engine.try_event()
            && predicate(&event)
        {
            return;
        }
        assert!(Instant::now() < deadline, "event timed out");
        std::thread::sleep(Duration::from_millis(1));
    }
}
fn disconnect_returning(sql: &str, fetch: bool) {
    returning_stop(sql, fetch, false);
}
fn returning_stop(sql: &str, fetch: bool, cancel: bool) {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("returning.sqlite");
    let setup = rusqlite::Connection::open(&path).unwrap();
    setup.execute_batch("CREATE TABLE work(id INTEGER PRIMARY KEY,value INTEGER NOT NULL); WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<300) INSERT INTO work SELECT x,0 FROM n;").unwrap();
    drop(setup);
    let mut engine = Engine::new(
        Default::default(),
        vec![Arc::new(choscordb_driver_sqlite::SqliteDriver)],
    )
    .unwrap();
    let connection = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: path.clone(),
                read_only: false,
            },
        )
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::Connected { .. }));
    let query = engine
        .execute(connection, sql.into(), QueryOptions::default())
        .unwrap();
    until(&mut engine, |e| matches!(e, Event::Schema { .. }));
    if fetch {
        engine
            .fetch_page(query, PageSize::new(100).unwrap())
            .unwrap();
        until(
            &mut engine,
            |e| matches!(e,Event::Page{page,..} if page.rows.len()==100 && page.has_more),
        );
    }
    if cancel {
        engine.cancel(query).unwrap();
        until(
            &mut engine,
            |e| matches!(e,Event::QueryFailed {error,..} if error.kind == choscordb_driver_api::ErrorKind::Cancelled),
        );
    } else {
        engine.disconnect(connection).unwrap();
        until(&mut engine, |e| matches!(e, Event::Disconnected { .. }));
    }
    let reopened = rusqlite::Connection::open(path).unwrap();
    let state: (i64, i64) = reopened
        .query_row("SELECT count(*),sum(value) FROM work", [], |row| {
            Ok((row.get(0)?, row.get(1)?))
        })
        .unwrap();
    assert_eq!(
        state,
        (300, 0),
        "unfinished RETURNING committed on disconnect (fetch={fetch})"
    );
}
#[test]
fn auto_insert_returning_disconnect_before_fetch_rolls_back() {
    disconnect_returning(
        "INSERT INTO work SELECT id+300,1 FROM work RETURNING id",
        false,
    );
}
#[test]
fn auto_insert_returning_disconnect_after_page_rolls_back() {
    disconnect_returning(
        "INSERT INTO work SELECT id+300,1 FROM work RETURNING id",
        true,
    );
}
#[test]
fn auto_update_returning_disconnect_before_fetch_rolls_back() {
    disconnect_returning("UPDATE work SET value=1 RETURNING id", false);
}
#[test]
fn auto_update_returning_disconnect_after_page_rolls_back() {
    disconnect_returning("UPDATE work SET value=1 RETURNING id", true);
}

#[test]
fn cancelled_auto_insert_returning_does_not_commit() {
    returning_stop(
        "INSERT INTO work SELECT id+300,1 FROM work RETURNING id",
        true,
        true,
    );
}
#[test]
fn cancelled_auto_update_returning_does_not_commit() {
    returning_stop("UPDATE work SET value=1 RETURNING id", true, true);
}
#[test]
fn direct_driver_close_aborts_returning_but_normal_finish_still_commits() {
    use choscordb_driver_api::DatabaseDriver;
    let runtime = tokio::runtime::Runtime::new().unwrap();
    for normal_finish in [false, true] {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("direct.sqlite");
        let db = rusqlite::Connection::open(&path).unwrap();
        db.execute_batch("CREATE TABLE work(id INTEGER PRIMARY KEY,value INTEGER); WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<300) INSERT INTO work SELECT x,0 FROM n;").unwrap();
        drop(db);
        runtime.block_on(async {
            let mut connection = choscordb_driver_sqlite::SqliteDriver
                .connect(ConnectionOptions::Sqlite {
                    path: path.clone(),
                    read_only: false,
                })
                .await
                .unwrap();
            let mut cursor = connection
                .execute(
                    "UPDATE work SET value=1 RETURNING id",
                    QueryOptions::default(),
                )
                .await
                .unwrap();
            let page = cursor
                .fetch_page(PageSize::new(100).unwrap())
                .await
                .unwrap();
            assert_eq!(page.rows.len(), 100);
            assert!(page.has_more);
            if normal_finish {
                cursor.close().await.unwrap();
            }
            connection.close().await.unwrap(); // No cancellation handle invocation.
        });
        let db = rusqlite::Connection::open(&path).unwrap();
        db.busy_timeout(Duration::from_secs(5)).unwrap();
        let sum: i64 = db
            .query_row("SELECT sum(value) FROM work", [], |row| row.get(0))
            .unwrap();
        assert_eq!(sum, if normal_finish { 300 } else { 0 });
    }
}
