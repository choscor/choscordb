//! Shared conformance checks through the application driver traits.
use choscordb_driver_api::*;

const BUDGET: usize = 64 * 1024;

async fn rows(connection: &mut dyn Connection, sql: &str, auto_commit: bool) -> Vec<Row> {
    let mut cursor = connection
        .execute_bounded(
            sql,
            QueryOptions {
                auto_commit,
                ..Default::default()
            },
            BUDGET,
        )
        .await
        .unwrap();
    let mut rows = Vec::new();
    loop {
        let page = cursor
            .fetch_page_bounded(PageSize::default(), BUDGET)
            .await
            .unwrap();
        assert!(page.estimated_bytes() <= BUDGET);
        rows.extend(page.rows);
        if !page.has_more {
            break;
        }
    }
    cursor.close().await.unwrap();
    rows
}

fn unique_table() -> String {
    format!(
        "driver_contract_{}_{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    )
}

pub async fn bounded_results(driver: &dyn DatabaseDriver, options: impl Fn() -> ConnectionOptions) {
    let mut connection = driver.connect(options()).await.unwrap();
    let table = unique_table();
    rows(
        &mut *connection,
        &format!("CREATE TABLE {table} (id BIGINT, label VARCHAR(32))"),
        true,
    )
    .await;
    for budget in [0, 1] {
        let error = connection
            .execute_bounded(
                &format!("INSERT INTO {table} VALUES (1, 'unwanted')"),
                QueryOptions::default(),
                budget,
            )
            .await
            .err()
            .expect("invalid schema budget must reject writes");
        assert_eq!(error.kind, ErrorKind::ResourceLimit);
    }
    assert_eq!(
        rows(
            &mut *connection,
            &format!("SELECT count(*) FROM {table}"),
            true
        )
        .await,
        vec![vec![Value::Integer(0)]]
    );
    rows(
        &mut *connection,
        &format!("INSERT INTO {table} VALUES (-7, 'alpha'), (42, NULL), (9007199254740993, '界')"),
        true,
    )
    .await;
    let mut cursor = connection
        .execute_bounded(
            &format!("SELECT id, label FROM {table} ORDER BY id"),
            QueryOptions::default(),
            BUDGET,
        )
        .await
        .unwrap();
    assert_eq!(
        cursor
            .columns()
            .iter()
            .map(|column| column.name.as_str())
            .collect::<Vec<_>>(),
        vec!["id", "label"]
    );
    for budget in [0, 1] {
        assert_eq!(
            cursor
                .fetch_page_bounded(PageSize::default(), budget)
                .await
                .unwrap_err()
                .kind,
            ErrorKind::ResourceLimit
        );
    }
    let page = cursor
        .fetch_page_bounded(PageSize::default(), BUDGET)
        .await
        .unwrap();
    assert_eq!(page.index, 0);
    assert_eq!(
        page.rows,
        vec![
            vec![Value::Integer(-7), Value::Text("alpha".into())],
            vec![Value::Integer(42), Value::Null],
            vec![Value::Integer(9007199254740993), Value::Text("界".into())]
        ]
    );
    assert!(!page.has_more);
    assert!(page.estimated_bytes() <= BUDGET);
    cursor.close().await.unwrap();
    let mut completed = connection
        .execute_bounded(
            &format!("UPDATE {table} SET label = 'updated' WHERE id = 42"),
            QueryOptions::default(),
            BUDGET,
        )
        .await
        .unwrap();
    assert_eq!(completed.summary().affected_rows, Some(1));
    assert_eq!(
        completed
            .fetch_page_bounded(PageSize::default(), 1)
            .await
            .unwrap_err()
            .kind,
        ErrorKind::ResourceLimit
    );
    let empty = completed
        .fetch_page_bounded(PageSize::default(), BUDGET)
        .await
        .unwrap();
    assert!(empty.rows.is_empty());
    assert!(!empty.has_more);
    completed.close().await.unwrap();
    rows(&mut *connection, &format!("DROP TABLE {table}"), true).await;
    connection.close().await.unwrap();
}

pub async fn close_rolls_back(
    driver: &dyn DatabaseDriver,
    options: impl Fn() -> ConnectionOptions,
) {
    let table = unique_table();
    let mut connection = driver.connect(options()).await.unwrap();
    rows(
        &mut *connection,
        &format!("CREATE TABLE {table} (id BIGINT)"),
        true,
    )
    .await;
    rows(
        &mut *connection,
        &format!("INSERT INTO {table} VALUES (7)"),
        false,
    )
    .await;
    assert_eq!(connection.transaction_state().await.unwrap(), Some(true));
    // Leave an auto-commit cursor alive: its finalizer must not commit the
    // unfinished transaction after the connection has been explicitly closed.
    let pending = connection
        .execute_bounded("SELECT 7", QueryOptions::default(), BUDGET)
        .await
        .unwrap();
    connection.close().await.unwrap();
    drop(pending);
    assert!(
        connection
            .execute_bounded("SELECT 1", QueryOptions::default(), BUDGET)
            .await
            .is_err(),
        "a closed session must reject further execution"
    );
    let mut fresh = driver.connect(options()).await.unwrap();
    assert_eq!(
        rows(&mut *fresh, &format!("SELECT count(*) FROM {table}"), true).await,
        vec![vec![Value::Integer(0)]]
    );
    rows(&mut *fresh, &format!("DROP TABLE {table}"), true).await;
    fresh.close().await.unwrap();
}

pub async fn cancellation_generation(
    driver: &dyn DatabaseDriver,
    options: impl Fn() -> ConnectionOptions,
) {
    let mut connection = driver.connect(options()).await.unwrap();
    let stale = connection.cancellation_handle();
    assert_eq!(
        rows(&mut *connection, "SELECT 17", true).await,
        vec![vec![Value::Integer(17)]]
    );
    stale.cancel().await.unwrap();
    assert_eq!(
        rows(&mut *connection, "SELECT 29", true).await,
        vec![vec![Value::Integer(29)]]
    );
    let fresh = connection.cancellation_handle();
    fresh.cancel().await.unwrap();
    let error = connection
        .execute_bounded("SELECT 41", QueryOptions::default(), BUDGET)
        .await
        .err()
        .expect("fresh cancellation must target the next execution");
    assert_eq!(error.kind, ErrorKind::Cancelled);
    assert_eq!(
        rows(&mut *connection, "SELECT 53", true).await,
        vec![vec![Value::Integer(53)]]
    );
    connection.close().await.unwrap();
}

/// An impossible schema budget is invalid input to execution, before native
/// dispatch, transaction setup, cursor replacement, or cancellation generation.
pub async fn schema_preflight_preserves_session(
    driver: &dyn DatabaseDriver,
    options: impl Fn() -> ConnectionOptions,
) {
    let mut connection = driver.connect(options()).await.unwrap();
    for budget in [0, 1] {
        let error = connection
            .execute_bounded(
                "SELECT 61",
                QueryOptions {
                    auto_commit: false,
                    ..Default::default()
                },
                budget,
            )
            .await
            .err()
            .expect("impossible schema budget must reject execution");
        assert_eq!(error.kind, ErrorKind::ResourceLimit);
        assert_eq!(
            connection.transaction_state().await.unwrap(),
            Some(false),
            "schema preflight must not start a manual transaction"
        );
    }
    let mut live = connection
        .execute_bounded("SELECT 73", QueryOptions::default(), BUDGET)
        .await
        .unwrap();
    let cancel = connection.cancellation_handle();
    for budget in [0, 1] {
        let error = connection
            .execute_bounded(
                "SELECT 83",
                QueryOptions {
                    auto_commit: false,
                    ..Default::default()
                },
                budget,
            )
            .await
            .err()
            .expect("impossible schema budget must reject execution");
        assert_eq!(error.kind, ErrorKind::ResourceLimit);
    }
    assert_eq!(
        live.fetch_page_bounded(PageSize::default(), BUDGET)
            .await
            .unwrap()
            .rows,
        vec![vec![Value::Integer(73)]],
        "schema preflight must preserve the existing cursor"
    );
    live.close().await.unwrap();
    cancel.cancel().await.unwrap();
    let error = connection
        .execute_bounded("SELECT 97", QueryOptions::default(), BUDGET)
        .await
        .err()
        .expect("schema preflight must preserve the acquired cancellation token");
    assert_eq!(error.kind, ErrorKind::Cancelled);
    connection.close().await.unwrap();
}
