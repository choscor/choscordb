use choscordb_driver_api::{Presentation, RecoveryHint, Tone, TransitionError, detail_or};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UpdaterState {
    Idle,
    Checking,
    UpToDate,
    Available,
    Downloading,
    ReadyToInstall,
    Installing,
    Cancelling,
    Cancelled,
    Failed,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UpdaterEvent {
    Check,
    NoUpdate,
    Found,
    Download,
    Downloaded,
    Install,
    Cancel,
    CancelSettled,
    Fail,
    Dismiss,
    Staged,
}

impl UpdaterState {
    pub const ALL: [Self; 10] = [
        Self::Idle,
        Self::Checking,
        Self::UpToDate,
        Self::Available,
        Self::Downloading,
        Self::ReadyToInstall,
        Self::Installing,
        Self::Cancelling,
        Self::Cancelled,
        Self::Failed,
    ];
}

impl UpdaterEvent {
    pub const ALL: [Self; 11] = [
        Self::Check,
        Self::NoUpdate,
        Self::Found,
        Self::Download,
        Self::Downloaded,
        Self::Install,
        Self::Cancel,
        Self::CancelSettled,
        Self::Fail,
        Self::Dismiss,
        Self::Staged,
    ];
}

pub fn step(
    state: &UpdaterState,
    event: &UpdaterEvent,
) -> Result<UpdaterState, TransitionError<UpdaterState, UpdaterEvent>> {
    use UpdaterEvent as E;
    use UpdaterState as S;
    let restartable = matches!(
        state,
        S::Idle | S::UpToDate | S::Available | S::Cancelled | S::Failed
    );
    let working = matches!(
        state,
        S::Checking | S::Downloading | S::Installing | S::Cancelling
    );
    match (*state, *event) {
        (_, E::Check) if restartable || *state == S::ReadyToInstall => Ok(S::Checking),
        (S::Checking | S::UpToDate | S::Available | S::Failed | S::Cancelled, E::Staged) => {
            Ok(S::ReadyToInstall)
        }
        (S::Checking, E::NoUpdate) => Ok(S::UpToDate),
        (S::Checking, E::Found) => Ok(S::Available),
        (S::Available, E::Download) => Ok(S::Downloading),
        (S::Downloading, E::Downloaded) => Ok(S::ReadyToInstall),
        (S::ReadyToInstall, E::Install) => Ok(S::Installing),
        (S::Checking | S::Downloading | S::Cancelling, E::Cancel) => Ok(S::Cancelling),
        (S::Cancelling, E::CancelSettled) => Ok(S::Cancelled),
        (_, E::Fail) if working => Ok(S::Failed),
        (_, E::Dismiss) if restartable || *state == S::ReadyToInstall => Ok(S::Idle),
        _ => Err(TransitionError {
            state: *state,
            event: *event,
        }),
    }
}

pub fn present(state: &UpdaterState, detail: &str) -> Presentation {
    use UpdaterState as S;
    match state {
        S::Idle => Presentation::idle(detail.trim()),
        S::Checking => Presentation::busy(detail_or(detail, "Checking for updates…")),
        S::UpToDate => Presentation::idle(detail_or(detail, "ChoscorDB is up to date."))
            .with_tone(Tone::Success),
        S::Available => Presentation::idle(detail_or(detail, "An update is available.")),
        S::Downloading => Presentation::busy(detail_or(detail, "Downloading the update…")),
        S::ReadyToInstall => Presentation::idle(detail_or(
            detail,
            "The update is downloaded. Choose Install Downloaded Update from the Updates menu \
                 when you are ready.",
        ))
        .with_tone(Tone::Success),
        S::Installing => Presentation::busy(detail_or(detail, "Installing the update…")),
        S::Cancelling => Presentation::busy(detail_or(detail, "Cancelling the update…")),
        S::Cancelled => Presentation::idle(detail_or(detail, "Update cancelled.")),
        S::Failed => {
            Presentation::failed("Update failed", detail, RecoveryHint::Retry).with_error_toast()
        }
    }
}
