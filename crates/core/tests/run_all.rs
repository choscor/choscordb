use choscordb_core::{RunAllStep, run_all_step};

#[test]
fn a_failed_statement_stops_run_all_only_when_the_preference_is_on() {
    assert_eq!(
        run_all_step(true, true),
        RunAllStep::Stop {
            message: "Run all stopped at the failed statement."
        }
    );
    assert_eq!(run_all_step(false, true), RunAllStep::Next);
    assert_eq!(run_all_step(true, false), RunAllStep::Next);
    assert_eq!(run_all_step(false, false), RunAllStep::Next);
}
