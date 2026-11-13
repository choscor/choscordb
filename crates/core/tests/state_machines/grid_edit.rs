use choscordb_core::grid_edit_machine::{
    EditPhase, GridEditEvent, GridEditState, TransactionPhase, present, step,
};
use choscordb_driver_api::{EDITS_NEED_NO_TRANSACTION, Tone};

fn state(edits: EditPhase, transaction: TransactionPhase) -> GridEditState {
    GridEditState { edits, transaction }
}

#[test]
fn step_matches_the_spec_transition_table() {
    crate::table::load("grid_edit").assert_step(&GridEditState::ALL, &GridEditEvent::ALL, step);
}

#[test]
fn presentation_matches_the_spec_table() {
    crate::table::load("grid_edit").assert_present(&GridEditState::ALL, |state| present(state, ""));
}

#[test]
fn an_open_transaction_blocks_apply_and_explains_why() {
    let pending = state(EditPhase::Pending, TransactionPhase::Active);
    assert!(step(&pending, &GridEditEvent::Apply).is_err());
    assert_eq!(present(&pending, "").message, EDITS_NEED_NO_TRANSACTION);
    assert_eq!(
        step(
            &state(EditPhase::Pending, TransactionPhase::Inactive),
            &GridEditEvent::Apply
        ),
        Ok(state(EditPhase::Applying, TransactionPhase::Inactive))
    );
}

#[test]
fn a_disconnect_during_apply_is_a_failure_and_ends_the_transaction() {
    assert_eq!(
        step(
            &state(EditPhase::Applying, TransactionPhase::Inactive),
            &GridEditEvent::Disconnect
        ),
        Ok(state(EditPhase::Failed, TransactionPhase::Inactive))
    );
    assert_eq!(
        step(
            &state(EditPhase::Pending, TransactionPhase::Active),
            &GridEditEvent::Disconnect
        ),
        Ok(state(EditPhase::Pending, TransactionPhase::Inactive))
    );
}

#[test]
fn a_failed_apply_ends_with_a_recovery_hint() {
    let failed = present(
        &state(EditPhase::Failed, TransactionPhase::Inactive),
        "duplicate key",
    );
    assert_eq!(failed.tone, Tone::Error);
    assert_eq!(
        failed.message,
        "Could not apply the grid changes: duplicate key. Retry."
    );
    assert_eq!(failed.toast, None);
}
