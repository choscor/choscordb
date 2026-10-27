use choscordb_driver_api::{EDITS_NEED_NO_TRANSACTION, transaction_guard};

#[test]
fn an_open_transaction_blocks_grid_edits() {
    let open = transaction_guard(true);
    assert_eq!(open.apply_edits, Some(EDITS_NEED_NO_TRANSACTION));
    let idle = transaction_guard(false);
    assert_eq!(idle.apply_edits, None);
}
