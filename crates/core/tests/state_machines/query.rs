use choscordb_core::query_machine::{QueryEvent, QueryState, failure_path, present, step};
use choscordb_driver_api::{Body, ErrorKind, Pane, Tone};

#[test]
fn step_matches_the_spec_transition_table() {
    crate::table::load("query").assert_step(&QueryState::ALL, &QueryEvent::ALL, step);
}

#[test]
fn presentation_matches_the_spec_table() {
    crate::table::load("query").assert_present(&QueryState::ALL, |state| present(state, ""));
}

#[test]
fn presentation_messages_are_engine_owned() {
    let golden = [
        (QueryState::Idle, "Run a query to see results."),
        (QueryState::Connecting, "Connecting…"),
        (QueryState::Queued, "Queued"),
        (QueryState::Running, "Running"),
        (QueryState::Cancelling, "Cancelling…"),
        (QueryState::Completed { empty: false }, "Completed"),
        (
            QueryState::Completed { empty: true },
            "Completed. The query returned no rows.",
        ),
        (QueryState::Failed, "Failed. Retry the query."),
        (QueryState::Cancelled, "Cancelled"),
        (
            QueryState::Disconnected,
            "Disconnected. Connect and run a query to see results.",
        ),
    ];
    assert_eq!(golden.len(), QueryState::ALL.len());
    for (state, message) in golden {
        assert_eq!(present(&state, "").message, message, "{state:?}");
    }
}

#[test]
fn a_failed_query_is_a_result_in_the_messages_pane_without_a_toast() {
    let failed = present(&QueryState::Failed, "syntax error near FROM.");
    assert_eq!(failed.tone, Tone::Error);
    assert_eq!(failed.pane, Pane::Messages);
    assert_ne!(failed.body, Body::Empty);
    assert_eq!(failed.toast, None);
    assert_eq!(
        failed.message,
        "Failed: syntax error near FROM. Retry the query."
    );
}

#[test]
fn a_detail_replaces_the_default_message_of_a_state() {
    assert_eq!(
        present(&QueryState::Running, "Loading result page…").message,
        "Loading result page…"
    );
    assert_eq!(present(&QueryState::Running, "  ").message, "Running");
}

#[test]
fn busy_states_are_neutral() {
    for state in QueryState::ALL {
        let shown = present(&state, "");
        if shown.busy {
            assert_eq!(shown.tone, Tone::Neutral, "{state:?}");
        }
    }
}

#[test]
fn a_disconnect_keeps_a_failure_visible() {
    assert_eq!(
        step(&QueryState::Failed, &QueryEvent::Disconnect),
        Ok(QueryState::Failed)
    );
    assert_eq!(
        step(
            &QueryState::Completed { empty: false },
            &QueryEvent::Disconnect
        ),
        Ok(QueryState::Disconnected)
    );
}

#[test]
fn an_illegal_event_is_a_typed_error() {
    let error = step(&QueryState::Idle, &QueryEvent::CancelSettled).unwrap_err();
    assert_eq!(error.state, QueryState::Idle);
    assert_eq!(error.event, QueryEvent::CancelSettled);
    assert_eq!(
        error.to_string(),
        "CancelSettled is not a valid event in the Idle state"
    );
}

#[test]
fn each_failure_path_is_a_walk_of_the_machine_from_every_state() {
    let kinds = [
        ErrorKind::Cancelled,
        ErrorKind::Timeout,
        ErrorKind::Disconnected,
        ErrorKind::Query,
        ErrorKind::Connection,
    ];
    for kind in kinds {
        for start in QueryState::ALL {
            let mut state = start;
            for target in failure_path(&start, kind) {
                assert!(
                    QueryEvent::ALL
                        .iter()
                        .any(|event| step(&state, event) == Ok(target)),
                    "{kind:?} from {start:?}: {state:?} cannot reach {target:?}"
                );
                assert_ne!(state, target, "{kind:?} from {start:?} repeats {state:?}");
                state = target;
            }
        }
    }
}

#[test]
fn a_failure_path_depends_on_the_state_of_the_query() {
    use QueryState as S;
    assert_eq!(
        failure_path(&S::Running, ErrorKind::Timeout),
        [S::Cancelling, S::Failed]
    );
    assert_eq!(
        failure_path(&S::Queued, ErrorKind::Cancelled),
        [S::Cancelling, S::Cancelled]
    );
    assert_eq!(
        failure_path(&S::Running, ErrorKind::Disconnected),
        [S::Failed]
    );
    let completed = S::Completed { empty: false };
    assert_eq!(failure_path(&completed, ErrorKind::Cancelled), [S::Failed]);
    assert_eq!(failure_path(&completed, ErrorKind::Timeout), [S::Failed]);
    assert_eq!(failure_path(&S::Failed, ErrorKind::Query), []);
    assert_eq!(failure_path(&S::Connecting, ErrorKind::Query), []);
    assert_eq!(failure_path(&S::Cancelled, ErrorKind::Query), []);
    assert_eq!(failure_path(&S::Cancelled, ErrorKind::Cancelled), []);
}

#[test]
fn a_result_is_empty_only_when_it_has_columns_and_no_rows() {
    use choscordb_core::query_machine::result_state;
    assert_eq!(result_state(true, 0), QueryState::Completed { empty: true });
    assert_eq!(
        result_state(true, 3),
        QueryState::Completed { empty: false }
    );
    assert_eq!(
        result_state(false, 0),
        QueryState::Completed { empty: false }
    );
}

#[test]
fn a_submission_that_fails_while_disconnected_shows_the_failure() {
    assert_eq!(
        step(&QueryState::Disconnected, &QueryEvent::Fail),
        Ok(QueryState::Failed)
    );
}
