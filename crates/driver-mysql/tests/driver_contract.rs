use choscordb_driver_api::*;
#[path = "support/server.rs"]
mod server;
fn settings() -> ConnectionOptions {
    server::options(TlsMode::Disable)
}
#[path = "../../driver-api/tests/support/driver_contract.rs"]
mod contract;

#[tokio::test]
#[ignore = "requires disposable MySQL or MariaDB fixture"]
async fn bounded_results() {
    contract::bounded_results(server::driver().as_ref(), settings).await;
}

#[tokio::test]
#[ignore = "requires disposable MySQL or MariaDB fixture"]
async fn close_rolls_back() {
    contract::close_rolls_back(server::driver().as_ref(), settings).await;
}

#[tokio::test]
#[ignore = "requires disposable MySQL or MariaDB fixture"]
async fn cancellation_generation() {
    contract::cancellation_generation(server::driver().as_ref(), settings).await;
}

#[tokio::test]
#[ignore = "requires disposable MySQL or MariaDB fixture"]
async fn schema_preflight_preserves_session() {
    contract::schema_preflight_preserves_session(server::driver().as_ref(), settings).await;
}
