use choscordb_driver_api::*;
use choscordb_driver_sqlite::SqliteDriver;
#[path = "../../driver-api/tests/support/driver_contract.rs"]
mod contract;

async fn check(which: u8) {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("contract.sqlite");
    let options = || ConnectionOptions::Sqlite {
        path: path.clone(),
        read_only: false,
    };
    match which {
        0 => contract::bounded_results(&SqliteDriver, options).await,
        1 => contract::close_rolls_back(&SqliteDriver, options).await,
        2 => contract::cancellation_generation(&SqliteDriver, options).await,
        _ => contract::schema_preflight_preserves_session(&SqliteDriver, options).await,
    }
}
#[tokio::test]
async fn bounded_results() {
    check(0).await;
}
#[tokio::test]
async fn close_rolls_back() {
    check(1).await;
}
#[tokio::test]
async fn cancellation_generation() {
    check(2).await;
}

#[tokio::test]
async fn schema_preflight_preserves_session() {
    check(3).await;
}
