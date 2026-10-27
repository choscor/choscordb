//! Round-trip budgets through a counting relay. Run with the disposable PostgreSQL fixture.
#[path = "../../driver-api/tests/support/round_trip_proxy.rs"]
#[allow(dead_code, reason = "Each test file uses part of the shared relay.")]
mod round_trip_proxy;

use choscordb_driver_api::*;
use choscordb_driver_postgres::PostgresDriver;
use round_trip_proxy::RoundTripProxy;

fn fixture() -> tokio_postgres::Config {
    std::env::var("CHOSCORDB_TEST_POSTGRES")
        .expect("CHOSCORDB_TEST_POSTGRES must name a disposable fixture")
        .parse()
        .unwrap()
}

async fn proxy() -> RoundTripProxy {
    let config = fixture();
    RoundTripProxy::start("127.0.0.1", config.get_ports()[0]).await
}

// Plain TCP through the relay, so that each protocol message is visible.
fn options(port: u16) -> ConnectionOptions {
    let config = fixture();
    ConnectionOptions::Postgres {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host: "127.0.0.1".into(),
        port,
        database: config.get_dbname().unwrap_or("postgres").into(),
        user: config.get_user().expect("fixture user").into(),
        password: config
            .get_password()
            .map(|value| Secret::new(std::str::from_utf8(value).unwrap())),
        ssh_secret: None,
        tls: TlsMode::Disable,
        ssh: None,
        tls_identity: None,
        root_certificate: None,
    }
}

async fn prepare_rows(table: &str, rows: usize, payload: usize) {
    let (admin, session) = tokio_postgres::connect(
        &std::env::var("CHOSCORDB_TEST_POSTGRES").unwrap(),
        tokio_postgres::NoTls,
    )
    .await
    .unwrap();
    let task = tokio::spawn(session);
    admin
        .batch_execute(&format!(
            "DROP TABLE IF EXISTS {table};
             CREATE TABLE {table} AS
             SELECT n::bigint AS id, repeat('x', {payload}) AS payload FROM generate_series(1, {rows}) AS n;"
        ))
        .await
        .unwrap();
    drop(admin);
    task.await.unwrap().unwrap();
}

fn ids(page: &ResultPage) -> Vec<i64> {
    page.rows
        .iter()
        .map(|row| match &row[0] {
            Value::Integer(value) => *value,
            other => panic!("unexpected id {other:?}"),
        })
        .collect()
}

#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn a_page_after_the_first_costs_at_most_two_turnarounds() {
    prepare_rows("round_trip_pages", 13_000, 8).await;
    let proxy = proxy().await;
    let mut connection = PostgresDriver.connect(options(proxy.port)).await.unwrap();
    let mut cursor = connection
        .execute(
            "SELECT id, payload FROM round_trip_pages ORDER BY id",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    let first = cursor
        .fetch_page(PageSize::new(1_000).unwrap())
        .await
        .unwrap();
    assert_eq!(ids(&first).first(), Some(&1));
    for size in [1_000, 10_000] {
        proxy.reset();
        let page = cursor
            .fetch_page(PageSize::new(size).unwrap())
            .await
            .unwrap();
        let turnarounds = proxy.turnarounds();
        println!("page of {size} rows: {turnarounds} turnarounds");
        assert_eq!(page.rows.len(), size as usize);
        assert!(page.has_more);
        assert!(turnarounds <= PAGE_BUDGET, "{turnarounds} turnarounds");
    }
    cursor.close().await.unwrap();
    connection.close().await.unwrap();
}

#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn a_byte_budget_stops_a_page_and_keeps_the_rest_for_the_next_page() {
    prepare_rows("round_trip_wide", 50, 4_000).await;
    let proxy = proxy().await;
    let mut connection = PostgresDriver.connect(options(proxy.port)).await.unwrap();
    let mut cursor = connection
        .execute(
            "SELECT id, payload FROM round_trip_wide ORDER BY id",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    let mut seen = Vec::new();
    let mut pages = 0;
    loop {
        let page = cursor
            .fetch_page_bounded(PageSize::new(1_000).unwrap(), 20_000)
            .await
            .unwrap();
        pages += 1;
        assert!(!page.rows.is_empty());
        seen.extend(ids(&page));
        if !page.has_more {
            break;
        }
    }
    // Each page stops early, and no row is lost or repeated.
    assert!(pages > 2, "{pages} pages");
    assert_eq!(seen, (1..=50).collect::<Vec<_>>());
    cursor.close().await.unwrap();
    connection.close().await.unwrap();
}

#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn a_select_and_its_inspection_fit_their_turnaround_budgets() {
    prepare_rows("round_trip_select", 20, 8).await;
    let proxy = proxy().await;
    let mut connection = PostgresDriver.connect(options(proxy.port)).await.unwrap();
    let sql = "SELECT id, payload FROM round_trip_select ORDER BY id";
    proxy.reset();
    let mut cursor = connection
        .execute(sql, QueryOptions::default())
        .await
        .unwrap();
    let page = cursor
        .fetch_page(PageSize::new(1_000).unwrap())
        .await
        .unwrap();
    let select = proxy.turnarounds();
    println!("select to first page: {select} turnarounds");
    assert_eq!(page.rows.len(), 20);
    cursor.close().await.unwrap();
    // The connection remembers the domain lookup of this result shape.
    proxy.reset();
    let mut cursor = connection
        .execute(sql, QueryOptions::default())
        .await
        .unwrap();
    cursor
        .fetch_page(PageSize::new(1_000).unwrap())
        .await
        .unwrap();
    let repeated = proxy.turnarounds();
    println!("repeated select to first page: {repeated} turnarounds");
    cursor.close().await.unwrap();
    let columns = vec!["id".to_owned(), "payload".to_owned()];
    proxy.reset();
    connection
        .inspect_edit_query(sql, columns.clone())
        .await
        .unwrap();
    connection
        .inspect_result_cells(None, sql, columns)
        .await
        .unwrap();
    let inspection = proxy.turnarounds();
    println!("inspection: {inspection} turnarounds");
    assert!(select <= SELECT_BUDGET, "{select} turnarounds");
    assert!(repeated <= REPEATED_SELECT_BUDGET, "{repeated} turnarounds");
    assert!(inspection <= INSPECTION_BUDGET, "{inspection} turnarounds");
    connection.close().await.unwrap();
}

async fn admin(sql: &str) {
    let (admin, session) = tokio_postgres::connect(
        &std::env::var("CHOSCORDB_TEST_POSTGRES").unwrap(),
        tokio_postgres::NoTls,
    )
    .await
    .unwrap();
    let task = tokio::spawn(session);
    admin.batch_execute(sql).await.unwrap();
    drop(admin);
    task.await.unwrap().unwrap();
}

async fn column_type(
    connection: &mut Box<dyn Connection>,
    sql: &str,
    options: QueryOptions,
) -> String {
    let mut cursor = connection.execute(sql, options).await.unwrap();
    let column = cursor.columns()[0].database_type.clone();
    cursor
        .fetch_page(PageSize::new(100).unwrap())
        .await
        .unwrap();
    cursor.close().await.unwrap();
    column
}

#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn a_column_type_change_on_the_connection_shows_the_new_domain() {
    admin(
        "DROP TABLE IF EXISTS round_trip_domain;
         DROP DOMAIN IF EXISTS round_trip_positive;
         CREATE DOMAIN round_trip_positive AS integer CHECK (VALUE > 0);
         CREATE TABLE round_trip_domain (n integer);
         INSERT INTO round_trip_domain VALUES (1);",
    )
    .await;
    let proxy = proxy().await;
    let mut connection = PostgresDriver.connect(options(proxy.port)).await.unwrap();
    let select = "SELECT n FROM round_trip_domain";
    let auto = QueryOptions::default();
    let manual = QueryOptions {
        auto_commit: false,
        ..Default::default()
    };
    assert_eq!(
        column_type(&mut connection, select, auto.clone()).await,
        "int4"
    );
    // DDL that rolls back leaves no remembered domain behind.
    connection
        .execute(
            "ALTER TABLE round_trip_domain ALTER COLUMN n TYPE round_trip_positive",
            manual.clone(),
        )
        .await
        .unwrap()
        .close()
        .await
        .unwrap();
    assert_eq!(
        column_type(&mut connection, select, manual).await,
        "round_trip_positive"
    );
    connection.rollback().await.unwrap();
    assert_eq!(
        column_type(&mut connection, select, auto.clone()).await,
        "int4"
    );
    // Committed DDL on the connection replaces the remembered lookup.
    connection
        .execute(
            "ALTER TABLE round_trip_domain ALTER COLUMN n TYPE round_trip_positive",
            auto.clone(),
        )
        .await
        .unwrap()
        .close()
        .await
        .unwrap();
    assert_eq!(
        column_type(&mut connection, select, auto).await,
        "round_trip_positive"
    );
    connection.close().await.unwrap();
}

async fn relation_id(table: &str) -> ObjectId {
    let (admin, session) = tokio_postgres::connect(
        &std::env::var("CHOSCORDB_TEST_POSTGRES").unwrap(),
        tokio_postgres::NoTls,
    )
    .await
    .unwrap();
    let task = tokio::spawn(session);
    let oid: u32 = admin
        .query_one(&format!("SELECT '{table}'::regclass::oid"), &[])
        .await
        .unwrap()
        .get(0);
    drop(admin);
    task.await.unwrap().unwrap();
    ObjectId(format!("pg:relation:{oid}"))
}

#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn each_later_object_page_reuses_the_prepared_read() {
    prepare_rows("round_trip_object", 300, 8).await;
    let object = relation_id("round_trip_object").await;
    let proxy = proxy().await;
    let mut connection = PostgresDriver.connect(options(proxy.port)).await.unwrap();
    let mut cursor = connection
        .open_object(&object, &ObjectDataTime::Current, 65_536)
        .await
        .unwrap();
    let size = PageSize::new(100).unwrap();
    let first = cursor.fetch_page_bounded(size, 1 << 20).await.unwrap();
    assert_eq!(ids(&first).len(), 100);
    proxy.reset();
    let second = cursor.fetch_page_bounded(size, 1 << 20).await.unwrap();
    let turnarounds = proxy.turnarounds();
    println!("later object page: {turnarounds} turnarounds");
    // LIMIT/OFFSET stays, so the page is the next 100 rows of the relation.
    assert_eq!(ids(&second).len(), 100);
    assert!(second.has_more);
    assert_eq!(turnarounds, 1, "{turnarounds} turnarounds");
    cursor.close().await.unwrap();
    connection.close().await.unwrap();
}

// One Execute fetches the page and the row that tells if more rows exist.
const PAGE_BUDGET: usize = 2;
// The spec budget of 3 is not possible, for these protocol reasons:
// 1. BEGIN, SET LOCAL statement_timeout and Parse share one turnaround.
// 2. The domain lookup needs the column origins that Parse returns.
// 3. Bind needs the result formats from the lookup. It shares a turnaround
//    with the first Execute, which reads one row: before that row, the driver
//    cannot know the row width, so it cannot size a page to the byte budget.
// 4. The page Execute.
// 5. COMMIT, after the result ends.
const SELECT_BUDGET: usize = 5;
// The connection remembers the lookup of step 2.
const REPEATED_SELECT_BUDGET: usize = 4;
// The catalog reads of both inspections run concurrently on one connection.
const INSPECTION_BUDGET: usize = 3;
