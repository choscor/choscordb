use choscordb_core::connection_machine::{
    CatalogSession, ConnectionEvent, ConnectionState, ReadSession, read_session, step,
};
use std::collections::BTreeSet;

#[test]
fn step_matches_the_spec_transition_table() {
    crate::table::load("connection").assert_step(
        &ConnectionState::ALL,
        &ConnectionEvent::ALL,
        step,
    );
}

#[test]
fn read_session_matches_the_spec_routing_table() {
    let actual: BTreeSet<String> = [false, true]
        .into_iter()
        .flat_map(|transaction| {
            CatalogSession::ALL.into_iter().map(move |catalog| {
                serde_json::json!([
                    transaction,
                    format!("{catalog:?}"),
                    format!("{:?}", read_session(transaction, catalog)),
                ])
                .to_string()
            })
        })
        .collect();
    assert_eq!(
        actual,
        crate::table::load("connection").rows("read_sessions")
    );
}

#[test]
fn an_open_transaction_keeps_navigator_reads_on_the_query_session() {
    assert_eq!(read_session(true, CatalogSession::Open), ReadSession::Query);
    assert_eq!(
        read_session(false, CatalogSession::Open),
        ReadSession::Catalog
    );
    assert_eq!(
        read_session(false, CatalogSession::RetryPending),
        ReadSession::Query
    );
}

#[test]
fn a_shutdown_while_connected_passes_through_disconnecting() {
    let connected = ConnectionState::Connected {
        transaction_active: true,
    };
    assert_eq!(
        step(&connected, &ConnectionEvent::Shutdown),
        Ok(ConnectionState::Disconnecting)
    );
    assert!(step(&ConnectionState::Failed, &ConnectionEvent::Opened).is_err());
}
