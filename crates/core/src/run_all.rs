#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RunAllStep {
    Next,
    Stop { message: &'static str },
}

pub fn run_all_step(stop_on_first_error: bool, failed: bool) -> RunAllStep {
    if stop_on_first_error && failed {
        RunAllStep::Stop {
            message: "Run all stopped at the failed statement.",
        }
    } else {
        RunAllStep::Next
    }
}
