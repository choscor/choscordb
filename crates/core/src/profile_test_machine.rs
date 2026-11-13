use choscordb_driver_api::{
    Body, Presentation, RecoveryHint, ToastVariant, Tone, TransitionError, detail_or, recovery_hint,
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
            .with_tone(Tone::Success)
            .with_toast(ToastVariant::Success),
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

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SshTrustNote {
    Inspecting,
    SettingsChanged,
    NoHostKeys,
    KeysInspected,
    ApprovalUnknown,
    ApprovalStale,
}

pub fn ssh_trust_note(note: SshTrustNote) -> String {
    match note {
        SshTrustNote::Inspecting => "Inspecting SSH host keys…".to_owned(),
        SshTrustNote::SettingsChanged => {
            "Connection settings changed. Inspect the host keys again.".to_owned()
        }
        SshTrustNote::NoHostKeys => "No SSH host keys were returned.".to_owned(),
        SshTrustNote::KeysInspected => {
            "SSH host keys inspected. Review the keys before approving.".to_owned()
        }
        SshTrustNote::ApprovalUnknown => "SSH host key approval outcome is unknown.".to_owned(),
        SshTrustNote::ApprovalStale => format!(
            "The approval request finished for the previous settings. {}",
            recovery_hint(RecoveryHint::InspectSshServer)
        ),
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProfileOperation {
    Loading,
    Saving,
    Duplicating,
    Deleting,
    Connecting,
    Listed,
    Saved,
    Deleted,
    Connected,
    Failed,
    ConnectUnsubmitted,
    ConnectionClosed,
    ProfileMissing,
    ListRefreshFailed,
}

pub fn present_operation(operation: ProfileOperation, detail: &str) -> Presentation {
    use ProfileOperation as O;
    match operation {
        O::Loading => Presentation::busy("Loading profiles…"),
        O::Saving => Presentation::busy("Saving profile…"),
        O::Duplicating => Presentation::busy("Duplicating profile…"),
        O::Deleting => Presentation::busy("Deleting profile…"),
        O::Connecting => Presentation::busy("Connecting…"),
        O::Listed => Presentation::idle(""),
        O::Saved => finished("Profile saved.", detail),
        O::Deleted => finished("Profile deleted.", detail),
        O::Connected => finished("Connected.", ""),
        O::Failed => Presentation::idle(detail.trim()).with_tone(Tone::Error),
        O::ConnectUnsubmitted => {
            Presentation::idle("Connection could not be submitted.").with_tone(Tone::Error)
        }
        O::ConnectionClosed => Presentation::idle(format!(
            "Connection closed before it was established. {}",
            recovery_hint(RecoveryHint::RetryConnection)
        ))
        .with_tone(Tone::Error),
        O::ProfileMissing => Presentation::idle(
            "The saved profile is no longer available. Refresh the profile list.",
        )
        .with_tone(Tone::Error),
        O::ListRefreshFailed => Presentation::idle(format!(
            "The change is stored, but the profile list could not refresh: {}. {}",
            detail.trim().trim_end_matches('.'),
            recovery_hint(RecoveryHint::Refresh)
        ))
        .with_tone(Tone::Error),
    }
}

fn finished(done: &str, warning: &str) -> Presentation {
    let warning = warning.trim();
    if warning.is_empty() {
        Presentation::idle(done)
            .with_tone(Tone::Success)
            .with_toast(ToastVariant::Success)
    } else {
        Presentation::idle(format!("{done} {warning}")).with_toast(ToastVariant::Warning)
    }
}
