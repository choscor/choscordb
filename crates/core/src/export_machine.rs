use choscordb_driver_api::{Body, Presentation, RecoveryHint, Tone, TransitionError, detail_or};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ExportState {
    Idle,
    Submitting,
    Running,
    Cancelling,
    Succeeded,
    Cancelled,
    Failed,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ExportEvent {
    Submit,
    Start,
    Cancel,
    Finish,
    Fail,
    CancelSettled,
    Reset,
}

impl ExportState {
    pub const ALL: [Self; 7] = [
        Self::Idle,
        Self::Submitting,
        Self::Running,
        Self::Cancelling,
        Self::Succeeded,
        Self::Cancelled,
        Self::Failed,
    ];
}

impl ExportEvent {
    pub const ALL: [Self; 7] = [
        Self::Submit,
        Self::Start,
        Self::Cancel,
        Self::Finish,
        Self::Fail,
        Self::CancelSettled,
        Self::Reset,
    ];
}

pub fn step(
    state: &ExportState,
    event: &ExportEvent,
) -> Result<ExportState, TransitionError<ExportState, ExportEvent>> {
    use ExportEvent as E;
    use ExportState as S;
    let working = matches!(state, S::Submitting | S::Running | S::Cancelling);
    let settled = matches!(state, S::Succeeded | S::Cancelled | S::Failed);
    match (*state, *event) {
        (_, E::Submit) if settled || *state == S::Idle => Ok(S::Submitting),
        (S::Submitting, E::Start) => Ok(S::Running),
        (_, E::Cancel) if working => Ok(S::Cancelling),
        (S::Running | S::Cancelling, E::Finish) => Ok(S::Succeeded),
        (_, E::Fail) if working => Ok(S::Failed),
        (S::Cancelling, E::CancelSettled) => Ok(S::Cancelled),
        (_, E::Reset) if settled => Ok(S::Idle),
        _ => Err(TransitionError {
            state: *state,
            event: *event,
        }),
    }
}

pub fn present(state: &ExportState, detail: &str) -> Presentation {
    use ExportState as S;
    match state {
        S::Idle => Presentation::idle(detail.trim()),
        S::Submitting => Presentation::busy(detail_or(detail, "Preparing the export…")),
        S::Running => Presentation::busy(detail_or(detail, "Exporting…")),
        S::Cancelling => Presentation::busy(detail_or(detail, "Cancelling the export…")),
        S::Succeeded => {
            Presentation::idle(detail_or(detail, "Export complete.")).with_tone(Tone::Success)
        }
        S::Cancelled => Presentation::idle(detail_or(
            detail,
            "Export cancelled. The partial file was removed.",
        )),
        S::Failed => Presentation {
            body: Body::None,
            ..Presentation::failed("Export failed", detail, RecoveryHint::Retry)
        },
    }
}
