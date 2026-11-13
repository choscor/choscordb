use choscordb_core::{export_machine, profile_test_machine, updater_machine};
use choscordb_driver_api::{Body, Tone};

#[test]
fn export_step_and_presentation_match_the_spec() {
    use export_machine::{ExportEvent, ExportState, present, step};
    let table = crate::table::load("export");
    table.assert_step(&ExportState::ALL, &ExportEvent::ALL, step);
    table.assert_present(&ExportState::ALL, |state| present(state, ""));
    assert_eq!(
        present(&ExportState::Failed, "disk full").message,
        "Export failed: disk full. Retry."
    );
}

#[test]
fn an_export_that_finishes_while_it_cancels_succeeds() {
    use export_machine::{ExportEvent, ExportState, step};
    assert_eq!(
        step(&ExportState::Cancelling, &ExportEvent::Finish),
        Ok(ExportState::Succeeded)
    );
}

#[test]
fn updater_step_and_presentation_match_the_spec() {
    use updater_machine::{UpdaterEvent, UpdaterState, present, step};
    let table = crate::table::load("updater");
    table.assert_step(&UpdaterState::ALL, &UpdaterEvent::ALL, step);
    table.assert_present(&UpdaterState::ALL, |state| present(state, ""));
}

#[test]
fn a_failed_update_check_shows_a_toast_and_a_placeholder() {
    use updater_machine::{UpdaterState, present};
    let failed = present(&UpdaterState::Failed, "feed signature is invalid");
    assert_eq!(failed.body, Body::FailedPlaceholder);
    assert_eq!(
        failed.message,
        "Update failed: feed signature is invalid. Retry."
    );
    assert_eq!(failed.toast.unwrap().text, failed.message);
}

#[test]
fn profile_test_step_and_presentation_match_the_spec() {
    use profile_test_machine::{ProfileTestEvent, ProfileTestState, present, step};
    let table = crate::table::load("profile_test");
    table.assert_step(&ProfileTestState::ALL, &ProfileTestEvent::ALL, step);
    table.assert_present(&ProfileTestState::ALL, |state| present(state, ""));
}

#[test]
fn a_running_connection_test_is_neutral() {
    use profile_test_machine::{ProfileTestState, present};
    let testing = present(&ProfileTestState::Testing, "");
    assert!(testing.busy);
    assert_eq!(testing.tone, Tone::Neutral);
    assert_eq!(
        present(&ProfileTestState::Trusted, "").message,
        "SSH host key approved. Retry the connection to use it."
    );
}
