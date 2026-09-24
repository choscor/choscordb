use choscordb_driver_api::*;
use choscordb_driver_sqlite::SqliteDriver;
async fn run(connection: &mut dyn Connection, sql: &str) {
    let mut cursor = connection
        .execute(
            sql,
            QueryOptions {
                auto_commit: false,
                ..Default::default()
            },
        )
        .await
        .unwrap();
    while cursor
        .fetch_page(PageSize::default())
        .await
        .unwrap()
        .has_more
    {}
    cursor.close().await.unwrap();
}
#[tokio::test]
async fn native_idle_snapshot_distinguishes_reads_writes_and_transaction_generations() {
    let mut connection = SqliteDriver
        .connect(ConnectionOptions::Sqlite {
            path: ":memory:".into(),
            read_only: false,
        })
        .await
        .unwrap();
    assert!(
        !connection
            .idle_transaction_state()
            .await
            .unwrap()
            .unwrap()
            .active
    );
    run(&mut *connection, "CREATE TABLE pending(value INTEGER)").await;
    connection.commit().await.unwrap();
    run(&mut *connection, "SELECT 1").await;
    let read = connection.idle_transaction_state().await.unwrap().unwrap();
    assert!(read.active && read.manual && !read.write_pending);
    assert!(read.started_at.is_some());
    run(
        &mut *connection,
        "INSERT INTO pending VALUES(1) RETURNING value",
    )
    .await;
    let write = connection.idle_transaction_state().await.unwrap().unwrap();
    assert!(write.write_pending);
    assert_eq!(write.started_at, read.started_at);
    connection.rollback().await.unwrap();
    assert!(
        !connection
            .idle_transaction_state()
            .await
            .unwrap()
            .unwrap()
            .active
    );
    run(&mut *connection, "SELECT count(*) FROM pending").await;
    let next = connection.idle_transaction_state().await.unwrap().unwrap();
    assert!(!next.write_pending);
    assert!(next.started_at > read.started_at);
    connection.close().await.unwrap();
}
#[tokio::test]
async fn explicit_immediate_lock_without_mutation_is_not_a_pending_write() {
    let mut connection = SqliteDriver
        .connect(ConnectionOptions::Sqlite {
            path: ":memory:".into(),
            read_only: false,
        })
        .await
        .unwrap();
    connection
        .execute("/* lock only */ BEGIN IMMEDIATE", QueryOptions::default())
        .await
        .unwrap()
        .close()
        .await
        .unwrap();
    let state = connection.idle_transaction_state().await.unwrap().unwrap();
    assert!(state.active && state.manual && !state.write_pending);
    connection.rollback().await.unwrap();
}
