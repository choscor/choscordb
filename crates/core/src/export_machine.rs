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

struct TransferTexts {
    preparing: &'static str,
    running: &'static str,
    cancelling: &'static str,
    complete: &'static str,
    cancelled: &'static str,
    failed: &'static str,
}

const EXPORT_TEXTS: TransferTexts = TransferTexts {
    preparing: "Preparing the export…",
    running: "Exporting…",
    cancelling: "Cancelling the export…",
    complete: "Export complete.",
    cancelled: "Export cancelled. The partial file was removed.",
    failed: "Export failed",
};

const IMPORT_TEXTS: TransferTexts = TransferTexts {
    preparing: "Preparing the import…",
    running: "Importing…",
    cancelling: "Cancelling the import…",
    complete: "Import complete.",
    cancelled: "Import cancelled. The table did not change.",
    failed: "Import failed",
};

fn present_transfer(state: &ExportState, detail: &str, texts: &TransferTexts) -> Presentation {
    use ExportState as S;
    match state {
        S::Idle => Presentation::idle(detail.trim()),
        S::Submitting => Presentation::busy(detail_or(detail, texts.preparing)),
        S::Running => Presentation::busy(detail_or(detail, texts.running)),
        S::Cancelling => Presentation::busy(detail_or(detail, texts.cancelling)),
        S::Succeeded => {
            Presentation::idle(detail_or(detail, texts.complete)).with_tone(Tone::Success)
        }
        S::Cancelled => Presentation::idle(detail_or(detail, texts.cancelled)),
        S::Failed => Presentation {
            body: Body::None,
            ..Presentation::failed(texts.failed, detail, RecoveryHint::Retry)
        },
    }
}

pub fn present(state: &ExportState, detail: &str) -> Presentation {
    present_transfer(state, detail, &EXPORT_TEXTS)
}

pub fn present_import(state: &ExportState, detail: &str) -> Presentation {
    present_transfer(state, detail, &IMPORT_TEXTS)
}
