use choscordb_driver_api::{
    AUTO_COMMIT_NEEDS_NO_TRANSACTION, EDITS_NEED_NO_TRANSACTION, transaction_guard,
};

#[test]
fn an_open_manual_transaction_blocks_grid_edits_and_auto_commit() {
    let open = transaction_guard(true);
    assert_eq!(open.apply_edits, Some(EDITS_NEED_NO_TRANSACTION));
    assert_eq!(
        open.enable_auto_commit,
        Some(AUTO_COMMIT_NEEDS_NO_TRANSACTION)
    );
    let idle = transaction_guard(false);
    assert_eq!(idle.apply_edits, None);
    assert_eq!(idle.enable_auto_commit, None);
}
