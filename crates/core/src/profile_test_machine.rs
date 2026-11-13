use choscordb_driver_api::{
    Body, Presentation, RecoveryHint, Tone, TransitionError, detail_or, recovery_hint,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProfileTestState {
    Idle,
    Testing,
    Succeeded,
    Failed,
    AwaitingTrust,
    Trusting,
    Trusted,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProfileTestEvent {
    Test,
    Passed,
    TestFailed,
    HostKeyUnknown,
    Approve,
    Approved,
    ApprovalFailed,
    Reject,
    Edit,
}

impl ProfileTestState {
    pub const ALL: [Self; 7] = [
        Self::Idle,
        Self::Testing,
        Self::Succeeded,
        Self::Failed,
        Self::AwaitingTrust,
        Self::Trusting,
        Self::Trusted,
    ];
}

impl ProfileTestEvent {
    pub const ALL: [Self; 9] = [
        Self::Test,
        Self::Passed,
        Self::TestFailed,
        Self::HostKeyUnknown,
        Self::Approve,
        Self::Approved,
        Self::ApprovalFailed,
        Self::Reject,
        Self::Edit,
    ];
}

pub fn step(
    state: &ProfileTestState,
    event: &ProfileTestEvent,
) -> Result<ProfileTestState, TransitionError<ProfileTestState, ProfileTestEvent>> {
    use ProfileTestEvent as E;
    use ProfileTestState as S;
    let restartable = matches!(state, S::Idle | S::Succeeded | S::Failed | S::Trusted);
    match (*state, *event) {
        (_, E::Test) if restartable => Ok(S::Testing),
        (S::Testing, E::Passed) => Ok(S::Succeeded),
        (S::Testing, E::TestFailed) | (S::Trusting, E::ApprovalFailed) => Ok(S::Failed),
        (S::Testing, E::HostKeyUnknown) => Ok(S::AwaitingTrust),
        (S::AwaitingTrust, E::Approve) => Ok(S::Trusting),
        (S::Trusting, E::Approved) => Ok(S::Trusted),
        (S::AwaitingTrust, E::Reject) => Ok(S::Idle),
        (_, E::Edit) if restartable || *state == S::AwaitingTrust => Ok(S::Idle),
        _ => Err(TransitionError {
            state: *state,
            event: *event,
        }),
    }
}

pub fn present(state: &ProfileTestState, detail: &str) -> Presentation {
    use ProfileTestState as S;
    match state {
        S::Idle => Presentation::idle(detail.trim()),
        S::Testing => Presentation::busy(detail_or(detail, "Testing the connection…")),
        S::Succeeded => Presentation::idle(detail_or(detail, "Connection test succeeded."))
            .with_tone(Tone::Success),
        S::Failed => Presentation {
            body: Body::None,
            ..Presentation::failed(
                "Connection test failed",
                detail,
                RecoveryHint::RetryConnection,
            )
        },
        S::AwaitingTrust => {
            Presentation::idle(detail_or(detail, "Approve the SSH host key to continue."))
        }
        S::Trusting => Presentation::busy(detail_or(detail, "Saving the SSH host key…")),
        S::Trusted => Presentation::idle(format!(
            "SSH host key approved. {}",
            recovery_hint(RecoveryHint::UseApprovedKey)
        ))
        .with_tone(Tone::Success),
    }
}
