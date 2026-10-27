//! Round-trip budgets through a counting relay. Run against the disposable MySQL server.
use choscordb_driver_api::*;
#[path = "../../driver-api/tests/support/round_trip_proxy.rs"]
#[allow(dead_code, reason = "Each test file uses part of the shared relay.")]
mod round_trip_proxy;
#[path = "support/server.rs"]
mod server;
use round_trip_proxy::RoundTripProxy;

fn through(proxy: &RoundTripProxy) -> ConnectionOptions {
    let mut options = server::options(TlsMode::Disable);
    match &mut options {
        ConnectionOptions::Mysql { port, .. } | ConnectionOptions::Mariadb { port, .. } => {
            *port = proxy.port
        }
        _ => unreachable!("the MySQL fixture options"),
    }
    options
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn connect_and_object_open_fit_their_budgets() {
    // A first login after a server start does the full password exchange. This one fills
    // the server's authentication cache, so the counted connect is the usual fast login.
    let mut warm = server::driver()
        .connect(server::options(TlsMode::Disable))
        .await
        .unwrap();
    warm.close().await.unwrap();
    let proxy = RoundTripProxy::start("127.0.0.1", server::port()).await;
    let mut connection = server::driver().connect(through(&proxy)).await.unwrap();
    // The engine reads the session mode at once after it connects.
    connection.sql_mode().await.unwrap().unwrap();
    let connect = proxy.turnarounds();
    println!("connect: {connect} turnarounds");
    assert_eq!(connect, BASELINE_CONNECT, "{connect} turnarounds");

    let table = object_table(&mut connection).await;
    let mut opened = Vec::new();
    for _ in 0..2 {
        let before = proxy.accepted();
        let mut cursor = connection
            .open_object(&table, &ObjectDataTime::Current, 1024 * 1024)
            .await
            .unwrap();
        cursor.fetch_page(PageSize::default()).await.unwrap();
        cursor.close().await.unwrap();
        opened.push(proxy.accepted() - before);
    }
    println!("object opens: {opened:?} new connections");
    assert_eq!(opened[1], BASELINE_SECOND_OPEN, "{opened:?}");
    connection.close().await.unwrap();
}

async fn run(connection: &mut Box<dyn Connection>, sql: &str) -> Result<()> {
    let mut cursor = connection.execute(sql, QueryOptions::default()).await?;
    let page = cursor.fetch_page(PageSize::default()).await;
    cursor.close().await?;
    page.map(drop)
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn a_known_sql_mode_needs_no_probe_before_a_run() {
    let proxy = RoundTripProxy::start("127.0.0.1", server::port()).await;
    let mut connection = server::driver().connect(through(&proxy)).await.unwrap();
    let mut probes = Vec::new();
    for _ in 0..2 {
        proxy.reset();
        let mode = connection.sql_mode().await.unwrap();
        probes.push(proxy.turnarounds());
        assert!(mode.is_some());
        run(&mut connection, "SELECT 1").await.unwrap();
    }
    println!("sql_mode probes before runs: {probes:?} turnarounds");
    assert_eq!(probes, [0, 0]);
    connection.close().await.unwrap();
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn the_known_sql_mode_follows_the_session() {
    let proxy = RoundTripProxy::start("127.0.0.1", server::port()).await;
    let mut connection = server::driver().connect(through(&proxy)).await.unwrap();
    let initial = connection.sql_mode().await.unwrap().unwrap();
    assert_ne!(initial, "ANSI_QUOTES");
    // A run that sets the mode reports the new mode with its result.
    run(&mut connection, "SET SESSION sql_mode = 'ANSI_QUOTES'")
        .await
        .unwrap();
    proxy.reset();
    assert_eq!(
        connection.sql_mode().await.unwrap().as_deref(),
        Some("ANSI_QUOTES")
    );
    assert_eq!(proxy.turnarounds(), 0);
    // After a failed run the mode is unknown, so the next request reads it again.
    run(
        &mut connection,
        "SET SESSION sql_mode = 'NO_BACKSLASH_ESCAPES'",
    )
    .await
    .unwrap();
    assert!(
        run(&mut connection, "SELECT * FROM round_trip_missing")
            .await
            .is_err()
    );
    proxy.reset();
    assert_eq!(
        connection.sql_mode().await.unwrap().as_deref(),
        Some("NO_BACKSLASH_ESCAPES")
    );
    assert!(proxy.turnarounds() >= 1);
    connection.close().await.unwrap();
    // A new connection reads the mode of its own new session.
    let mut reconnected = server::driver().connect(through(&proxy)).await.unwrap();
    assert_eq!(
        reconnected.sql_mode().await.unwrap().as_deref(),
        Some(initial.as_str())
    );
    reconnected.close().await.unwrap();
}

// Measured budgets. A connect is one authentication turnaround and one query, which
// also reads the session mode.
const BASELINE_CONNECT: usize = 2;
// The connection keeps one idle object connection for the next open.
const BASELINE_SECOND_OPEN: usize = 0;

/// The table that the object tests open.
async fn object_table(connection: &mut Box<dyn Connection>) -> ObjectId {
    connection
        .execute(
            "CREATE TABLE IF NOT EXISTS round_trip_objects (id INT PRIMARY KEY)",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    let database = connection
        .load_metadata(None)
        .await
        .unwrap()
        .into_iter()
        .find(|object| object.name == "choscordb_test")
        .unwrap();
    connection
        .load_metadata(Some(database.id))
        .await
        .unwrap()
        .into_iter()
        .find(|object| object.name == "round_trip_objects")
        .unwrap()
        .id
}

#[tokio::test]
#[ignore = "requires disposable MySQL server on localhost:33306"]
async fn an_idle_object_connection_does_not_use_the_transport_budget() {
    // Four connections each keep an idle object connection after their tab closed.
    let mut connections = Vec::new();
    for _ in 0..4 {
        let mut connection = server::driver()
            .connect(server::options(TlsMode::Disable))
            .await
            .unwrap();
        let table = object_table(&mut connection).await;
        let mut cursor = connection
            .open_object(&table, &ObjectDataTime::Current, 1024 * 1024)
            .await
            .unwrap();
        cursor.close().await.unwrap();
        connections.push(connection);
    }
    // No object tab is open, so a fifth connection fits.
    let mut fifth = server::driver()
        .connect(server::options(TlsMode::Disable))
        .await
        .unwrap();
    fifth.close().await.unwrap();
    for mut connection in connections {
        connection.close().await.unwrap();
    }
}
