use crate::ffi;
use choscordb_core::{
    export_machine::{self, ExportEvent, ExportState},
    grid_edit_machine::{self, EditPhase, GridEditEvent, GridEditState, TransactionPhase},
    panel_machine::{self, PanelEvent, PanelFacts, PanelKind, PanelState, PanelWork},
    profile_test_machine::{self, ProfileTestEvent, ProfileTestState},
    query_machine::{self, QueryEvent, QueryState},
    updater_machine::{self, UpdaterEvent, UpdaterState},
};
use choscordb_driver_api::{
    Body, Pane, Presentation, RetryAction, ToastVariant, Tone, TransitionError,
};
use std::fmt::Debug;

impl Default for ffi::QueryStateDto {
    fn default() -> Self {
        Self::Idle
    }
}

fn unknown(what: &str, repr: u8) -> String {
    format!("Unknown {what} {repr}.")
}

fn presentation(shown: Presentation) -> ffi::PresentationDto {
    let (has_toast, toast, toast_variant) = match shown.toast {
        Some(toast) => (
            true,
            toast.text,
            match toast.variant {
                ToastVariant::Error => ffi::ToastVariantDto::Error,
                ToastVariant::Success => ffi::ToastVariantDto::Success,
                ToastVariant::Warning => ffi::ToastVariantDto::Warning,
            },
        ),
        None => (false, String::new(), ffi::ToastVariantDto::Error),
    };
    ffi::PresentationDto {
        tone: match shown.tone {
            Tone::Neutral => ffi::ToneDto::Neutral,
            Tone::Success => ffi::ToneDto::Success,
            Tone::Error => ffi::ToneDto::Error,
        },
        busy: shown.busy,
        body: match shown.body {
            Body::Content => ffi::BodyDto::Content,
            Body::Empty => ffi::BodyDto::Empty,
            Body::NoMatch => ffi::BodyDto::NoMatch,
            Body::FailedPlaceholder => ffi::BodyDto::FailedPlaceholder,
            Body::None => ffi::BodyDto::None,
        },
        pane: match shown.pane {
            Pane::Grid => ffi::PaneDto::Grid,
            Pane::Messages => ffi::PaneDto::Messages,
            Pane::Unchanged => ffi::PaneDto::Unchanged,
        },
        retry: match shown.retry {
            RetryAction::None => ffi::RowRetryDto::None,
            RetryAction::Refresh => ffi::RowRetryDto::Refresh,
        },
        message: shown.message,
        has_toast,
        toast,
        toast_variant,
    }
}

fn invalid(message: String) -> ffi::PresentationDto {
    presentation(Presentation::failed(
        &message,
        "",
        choscordb_driver_api::RecoveryHint::Retry,
    ))
}

fn transition<S: Copy + Debug, E: Debug>(
    state: S,
    result: Result<S, TransitionError<S, E>>,
) -> (bool, S, String) {
    match result {
        Ok(next) => (true, next, String::new()),
        Err(error) => (false, state, error.to_string()),
    }
}

fn query_state(dto: ffi::QueryStateDto) -> Result<QueryState, String> {
    use ffi::QueryStateDto as D;
    Ok(match dto {
        D::Idle => QueryState::Idle,
        D::Connecting => QueryState::Connecting,
        D::Queued => QueryState::Queued,
        D::Running => QueryState::Running,
        D::Cancelling => QueryState::Cancelling,
        D::CompletedRows => QueryState::Completed { empty: false },
        D::CompletedEmpty => QueryState::Completed { empty: true },
        D::Failed => QueryState::Failed,
        D::Cancelled => QueryState::Cancelled,
        D::Disconnected => QueryState::Disconnected,
        _ => return Err(unknown("query state", dto.repr)),
    })
}

pub(crate) fn query_state_dto(state: QueryState) -> ffi::QueryStateDto {
    use ffi::QueryStateDto as D;
    match state {
        QueryState::Idle => D::Idle,
        QueryState::Connecting => D::Connecting,
        QueryState::Queued => D::Queued,
        QueryState::Running => D::Running,
        QueryState::Cancelling => D::Cancelling,
        QueryState::Completed { empty: false } => D::CompletedRows,
        QueryState::Completed { empty: true } => D::CompletedEmpty,
        QueryState::Failed => D::Failed,
        QueryState::Cancelled => D::Cancelled,
        QueryState::Disconnected => D::Disconnected,
    }
}

fn query_event(dto: ffi::QueryEventDto) -> Result<QueryEvent, String> {
    use ffi::QueryEventDto as D;
    Ok(match dto {
        D::Connect => QueryEvent::Connect,
        D::SessionReady => QueryEvent::SessionReady,
        D::ConnectFailed => QueryEvent::ConnectFailed,
        D::Submit => QueryEvent::Submit,
        D::Start => QueryEvent::Start,
        D::Fetch => QueryEvent::Fetch,
        D::Cancel => QueryEvent::Cancel,
        D::TimedOut => QueryEvent::TimedOut,
        D::FinishRows => QueryEvent::Finish { empty: false },
        D::FinishEmpty => QueryEvent::Finish { empty: true },
        D::Fail => QueryEvent::Fail,
        D::CancelSettled => QueryEvent::CancelSettled,
        D::Disconnect => QueryEvent::Disconnect,
        D::Reset => QueryEvent::Reset,
        _ => return Err(unknown("query event", dto.repr)),
    })
}

pub fn query_presentation(state: ffi::QueryStateDto, detail: &str) -> ffi::PresentationDto {
    match query_state(state) {
        Ok(state) => presentation(query_machine::present(&state, detail)),
        Err(error) => invalid(error),
    }
}

pub fn query_result_state(has_columns: bool, rows: u64) -> ffi::QueryStateDto {
    query_state_dto(query_machine::result_state(has_columns, rows))
}

pub fn query_transition(
    state: ffi::QueryStateDto,
    event: ffi::QueryEventDto,
) -> ffi::QueryTransitionDto {
    let (allowed, next, error) = match (query_state(state), query_event(event)) {
        (Ok(current), Ok(event)) => transition(current, query_machine::step(&current, &event)),
        (Err(error), _) | (_, Err(error)) => (false, QueryState::Failed, error),
    };
    ffi::QueryTransitionDto {
        allowed,
        state: query_state_dto(next),
        error,
    }
}

fn panel_kind(dto: ffi::PanelKindDto) -> Result<PanelKind, String> {
    use ffi::PanelKindDto as D;
    Ok(match dto {
        D::SavedSql => PanelKind::SavedSql,
        D::History => PanelKind::History,
        D::Pins => PanelKind::Pins,
        D::Objects => PanelKind::Objects,
        D::ObjectDetails => PanelKind::ObjectDetails,
        D::Diagram => PanelKind::Diagram,
        D::QuickSearch => PanelKind::QuickSearch,
        _ => return Err(unknown("panel kind", dto.repr)),
    })
}

fn panel_state(dto: ffi::PanelStateDto) -> Result<PanelState, String> {
    use ffi::PanelStateDto as D;
    Ok(match dto {
        D::Idle => PanelState::Idle,
        D::Loading => PanelState::Loading,
        D::Ready => PanelState::Ready,
        D::Empty => PanelState::Empty,
        D::NoMatch => PanelState::NoMatch,
        D::Failed => PanelState::Failed,
        D::Disconnected => PanelState::Disconnected,
        D::Incomplete => PanelState::Incomplete,
        D::Unsupported => PanelState::Unsupported,
        _ => return Err(unknown("panel state", dto.repr)),
    })
}

fn panel_state_dto(state: PanelState) -> ffi::PanelStateDto {
    use ffi::PanelStateDto as D;
    match state {
        PanelState::Idle => D::Idle,
        PanelState::Loading => D::Loading,
        PanelState::Ready => D::Ready,
        PanelState::Empty => D::Empty,
        PanelState::NoMatch => D::NoMatch,
        PanelState::Failed => D::Failed,
        PanelState::Disconnected => D::Disconnected,
        PanelState::Incomplete => D::Incomplete,
        PanelState::Unsupported => D::Unsupported,
    }
}

fn panel_event(dto: ffi::PanelEventDto) -> Result<PanelEvent, String> {
    use ffi::PanelEventDto as D;
    Ok(match dto {
        D::Load => PanelEvent::Load,
        D::LoadedRows => PanelEvent::LoadedRows,
        D::LoadedNone => PanelEvent::LoadedNone,
        D::LoadedPartial => PanelEvent::LoadedPartial,
        D::LoadUnsupported => PanelEvent::LoadUnsupported,
        D::LoadFailed => PanelEvent::LoadFailed,
        D::FilterMatched => PanelEvent::FilterMatched,
        D::FilterMissed => PanelEvent::FilterMissed,
        D::Disconnect => PanelEvent::Disconnect,
        _ => return Err(unknown("panel event", dto.repr)),
    })
}

pub fn panel_presentation(
    kind: ffi::PanelKindDto,
    state: ffi::PanelStateDto,
    detail: &str,
) -> ffi::PresentationDto {
    match (panel_kind(kind), panel_state(state)) {
        (Ok(kind), Ok(state)) => presentation(panel_machine::present(kind, &state, detail)),
        (Err(error), _) | (_, Err(error)) => invalid(error),
    }
}

pub fn panel_transition(
    state: ffi::PanelStateDto,
    event: ffi::PanelEventDto,
) -> ffi::PanelTransitionDto {
    let (allowed, next, error) = match (panel_state(state), panel_event(event)) {
        (Ok(current), Ok(event)) => transition(current, panel_machine::step(&current, &event)),
        (Err(error), _) | (_, Err(error)) => (false, PanelState::Failed, error),
    };
    ffi::PanelTransitionDto {
        allowed,
        state: panel_state_dto(next),
        error,
    }
}

fn panel_work(dto: ffi::PanelWorkDto) -> Result<Option<PanelWork>, String> {
    use ffi::PanelWorkDto as D;
    Ok(match dto {
        D::None => None,
        D::Loading => Some(PanelWork::Loading),
        D::Clearing => Some(PanelWork::Clearing),
        D::Saving => Some(PanelWork::Saving),
        _ => return Err(unknown("panel work", dto.repr)),
    })
}

pub fn panel_observation(
    kind: ffi::PanelKindDto,
    facts: &ffi::PanelFactsDto,
    error: &str,
) -> ffi::PanelObservationDto {
    match (panel_kind(kind), panel_work(facts.work)) {
        (Ok(kind), Ok(work)) => {
            let facts = PanelFacts {
                failed: facts.failed,
                work,
                loaded: facts.loaded,
                has_rows: facts.has_rows,
                filtered: facts.filtered,
                paged: facts.paged,
                disconnected: facts.disconnected,
            };
            let (state, shown) = panel_machine::observe(kind, &facts, error);
            ffi::PanelObservationDto {
                state: panel_state_dto(state),
                presentation: presentation(shown),
            }
        }
        (Err(error), _) | (_, Err(error)) => ffi::PanelObservationDto {
            state: panel_state_dto(PanelState::Failed),
            presentation: invalid(error),
        },
    }
}

fn export_state(dto: ffi::ExportStateDto) -> Result<ExportState, String> {
    use ffi::ExportStateDto as D;
    Ok(match dto {
        D::Idle => ExportState::Idle,
        D::Submitting => ExportState::Submitting,
        D::Running => ExportState::Running,
        D::Cancelling => ExportState::Cancelling,
        D::Succeeded => ExportState::Succeeded,
        D::Cancelled => ExportState::Cancelled,
        D::Failed => ExportState::Failed,
        _ => return Err(unknown("export state", dto.repr)),
    })
}

fn export_state_dto(state: ExportState) -> ffi::ExportStateDto {
    use ffi::ExportStateDto as D;
    match state {
        ExportState::Idle => D::Idle,
        ExportState::Submitting => D::Submitting,
        ExportState::Running => D::Running,
        ExportState::Cancelling => D::Cancelling,
        ExportState::Succeeded => D::Succeeded,
        ExportState::Cancelled => D::Cancelled,
        ExportState::Failed => D::Failed,
    }
}

fn export_event(dto: ffi::ExportEventDto) -> Result<ExportEvent, String> {
    use ffi::ExportEventDto as D;
    Ok(match dto {
        D::Submit => ExportEvent::Submit,
        D::Start => ExportEvent::Start,
        D::Cancel => ExportEvent::Cancel,
        D::Finish => ExportEvent::Finish,
        D::Fail => ExportEvent::Fail,
        D::CancelSettled => ExportEvent::CancelSettled,
        D::Reset => ExportEvent::Reset,
        _ => return Err(unknown("export event", dto.repr)),
    })
}

pub fn export_presentation(state: ffi::ExportStateDto, detail: &str) -> ffi::PresentationDto {
    match export_state(state) {
        Ok(state) => presentation(export_machine::present(&state, detail)),
        Err(error) => invalid(error),
    }
}

pub fn export_transition(
    state: ffi::ExportStateDto,
    event: ffi::ExportEventDto,
) -> ffi::ExportTransitionDto {
    let (allowed, next, error) = match (export_state(state), export_event(event)) {
        (Ok(current), Ok(event)) => transition(current, export_machine::step(&current, &event)),
        (Err(error), _) | (_, Err(error)) => (false, ExportState::Failed, error),
    };
    ffi::ExportTransitionDto {
        allowed,
        state: export_state_dto(next),
        error,
    }
}

fn profile_test_state(dto: ffi::ProfileTestStateDto) -> Result<ProfileTestState, String> {
    use ffi::ProfileTestStateDto as D;
    Ok(match dto {
        D::Idle => ProfileTestState::Idle,
        D::Testing => ProfileTestState::Testing,
        D::Succeeded => ProfileTestState::Succeeded,
        D::Failed => ProfileTestState::Failed,
        D::AwaitingTrust => ProfileTestState::AwaitingTrust,
        D::Trusting => ProfileTestState::Trusting,
        D::Trusted => ProfileTestState::Trusted,
        _ => return Err(unknown("profile test state", dto.repr)),
    })
}

fn profile_test_state_dto(state: ProfileTestState) -> ffi::ProfileTestStateDto {
    use ffi::ProfileTestStateDto as D;
    match state {
        ProfileTestState::Idle => D::Idle,
        ProfileTestState::Testing => D::Testing,
        ProfileTestState::Succeeded => D::Succeeded,
        ProfileTestState::Failed => D::Failed,
        ProfileTestState::AwaitingTrust => D::AwaitingTrust,
        ProfileTestState::Trusting => D::Trusting,
        ProfileTestState::Trusted => D::Trusted,
    }
}

fn profile_test_event(dto: ffi::ProfileTestEventDto) -> Result<ProfileTestEvent, String> {
    use ffi::ProfileTestEventDto as D;
    Ok(match dto {
        D::Test => ProfileTestEvent::Test,
        D::Passed => ProfileTestEvent::Passed,
        D::TestFailed => ProfileTestEvent::TestFailed,
        D::HostKeyUnknown => ProfileTestEvent::HostKeyUnknown,
        D::Approve => ProfileTestEvent::Approve,
        D::Approved => ProfileTestEvent::Approved,
        D::ApprovalFailed => ProfileTestEvent::ApprovalFailed,
        D::Reject => ProfileTestEvent::Reject,
        D::Edit => ProfileTestEvent::Edit,
        _ => return Err(unknown("profile test event", dto.repr)),
    })
}

pub fn profile_test_transition(
    state: ffi::ProfileTestStateDto,
    event: ffi::ProfileTestEventDto,
) -> ffi::ProfileTestTransitionDto {
    let (allowed, next, error) = match (profile_test_state(state), profile_test_event(event)) {
        (Ok(current), Ok(event)) => {
            transition(current, profile_test_machine::step(&current, &event))
        }
        (Err(error), _) | (_, Err(error)) => (false, ProfileTestState::Failed, error),
    };
    ffi::ProfileTestTransitionDto {
        allowed,
        state: profile_test_state_dto(next),
        error,
    }
}

pub fn profile_test_presentation(
    state: ffi::ProfileTestStateDto,
    detail: &str,
) -> ffi::PresentationDto {
    match profile_test_state(state) {
        Ok(state) => presentation(profile_test_machine::present(&state, detail)),
        Err(error) => invalid(error),
    }
}

fn grid_edit_state(dto: ffi::GridEditStateDto) -> Result<GridEditState, String> {
    use ffi::{EditPhaseDto as E, TransactionPhaseDto as T};
    let edits = match dto.edits {
        E::Clean => EditPhase::Clean,
        E::Pending => EditPhase::Pending,
        E::Planning => EditPhase::Planning,
        E::Applying => EditPhase::Applying,
        E::Failed => EditPhase::Failed,
        _ => return Err(unknown("edit phase", dto.edits.repr)),
    };
    let transaction = match dto.transaction {
        T::Inactive => TransactionPhase::Inactive,
        T::Active => TransactionPhase::Active,
        T::Ending => TransactionPhase::Ending,
        _ => return Err(unknown("transaction phase", dto.transaction.repr)),
    };
    Ok(GridEditState { edits, transaction })
}

fn grid_edit_state_dto(state: GridEditState) -> ffi::GridEditStateDto {
    use ffi::{EditPhaseDto as E, TransactionPhaseDto as T};
    ffi::GridEditStateDto {
        edits: match state.edits {
            EditPhase::Clean => E::Clean,
            EditPhase::Pending => E::Pending,
            EditPhase::Planning => E::Planning,
            EditPhase::Applying => E::Applying,
            EditPhase::Failed => E::Failed,
        },
        transaction: match state.transaction {
            TransactionPhase::Inactive => T::Inactive,
            TransactionPhase::Active => T::Active,
            TransactionPhase::Ending => T::Ending,
        },
    }
}

fn grid_edit_event(dto: ffi::GridEditEventDto) -> Result<GridEditEvent, String> {
    use ffi::GridEditEventDto as D;
    Ok(match dto {
        D::Edit => GridEditEvent::Edit,
        D::Plan => GridEditEvent::Plan,
        D::Planned => GridEditEvent::Planned,
        D::PlanFailed => GridEditEvent::PlanFailed,
        D::Apply => GridEditEvent::Apply,
        D::Applied => GridEditEvent::Applied,
        D::ApplyFailed => GridEditEvent::ApplyFailed,
        D::Discard => GridEditEvent::Discard,
        D::Begin => GridEditEvent::Begin,
        D::Commit => GridEditEvent::Commit,
        D::Rollback => GridEditEvent::Rollback,
        D::Ended => GridEditEvent::Ended,
        D::EndFailed => GridEditEvent::EndFailed,
        D::Disconnect => GridEditEvent::Disconnect,
        _ => return Err(unknown("grid edit event", dto.repr)),
    })
}

pub fn grid_edit_presentation(state: ffi::GridEditStateDto, detail: &str) -> ffi::PresentationDto {
    match grid_edit_state(state) {
        Ok(state) => presentation(grid_edit_machine::present(&state, detail)),
        Err(error) => invalid(error),
    }
}

pub fn grid_edit_transition(
    state: ffi::GridEditStateDto,
    event: ffi::GridEditEventDto,
) -> ffi::GridEditTransitionDto {
    match (grid_edit_state(state), grid_edit_event(event)) {
        (Ok(current), Ok(event)) => {
            let (allowed, next, error) =
                transition(current, grid_edit_machine::step(&current, &event));
            ffi::GridEditTransitionDto {
                allowed,
                state: grid_edit_state_dto(next),
                error,
            }
        }
        (Err(error), _) | (_, Err(error)) => ffi::GridEditTransitionDto {
            allowed: false,
            state,
            error,
        },
    }
}

fn updater_state(dto: ffi::UpdaterStateDto) -> Result<UpdaterState, String> {
    use ffi::UpdaterStateDto as D;
    Ok(match dto {
        D::Idle => UpdaterState::Idle,
        D::Checking => UpdaterState::Checking,
        D::UpToDate => UpdaterState::UpToDate,
        D::Available => UpdaterState::Available,
        D::Downloading => UpdaterState::Downloading,
        D::ReadyToInstall => UpdaterState::ReadyToInstall,
        D::Installing => UpdaterState::Installing,
        D::Cancelling => UpdaterState::Cancelling,
        D::Cancelled => UpdaterState::Cancelled,
        D::Failed => UpdaterState::Failed,
        _ => return Err(unknown("updater state", dto.repr)),
    })
}

fn updater_state_dto(state: UpdaterState) -> ffi::UpdaterStateDto {
    use ffi::UpdaterStateDto as D;
    match state {
        UpdaterState::Idle => D::Idle,
        UpdaterState::Checking => D::Checking,
        UpdaterState::UpToDate => D::UpToDate,
        UpdaterState::Available => D::Available,
        UpdaterState::Downloading => D::Downloading,
        UpdaterState::ReadyToInstall => D::ReadyToInstall,
        UpdaterState::Installing => D::Installing,
        UpdaterState::Cancelling => D::Cancelling,
        UpdaterState::Cancelled => D::Cancelled,
        UpdaterState::Failed => D::Failed,
    }
}

fn updater_event(dto: ffi::UpdaterEventDto) -> Result<UpdaterEvent, String> {
    use ffi::UpdaterEventDto as D;
    Ok(match dto {
        D::Check => UpdaterEvent::Check,
        D::NoUpdate => UpdaterEvent::NoUpdate,
        D::Found => UpdaterEvent::Found,
        D::Download => UpdaterEvent::Download,
        D::Downloaded => UpdaterEvent::Downloaded,
        D::Install => UpdaterEvent::Install,
        D::Cancel => UpdaterEvent::Cancel,
        D::CancelSettled => UpdaterEvent::CancelSettled,
        D::Fail => UpdaterEvent::Fail,
        D::Dismiss => UpdaterEvent::Dismiss,
        D::Staged => UpdaterEvent::Staged,
        _ => return Err(unknown("updater event", dto.repr)),
    })
}

pub fn updater_presentation(state: ffi::UpdaterStateDto, detail: &str) -> ffi::PresentationDto {
    match updater_state(state) {
        Ok(state) => presentation(updater_machine::present(&state, detail)),
        Err(error) => invalid(error),
    }
}

pub fn updater_transition(
    state: ffi::UpdaterStateDto,
    event: ffi::UpdaterEventDto,
) -> ffi::UpdaterTransitionDto {
    let (allowed, next, error) = match (updater_state(state), updater_event(event)) {
        (Ok(current), Ok(event)) => transition(current, updater_machine::step(&current, &event)),
        (Err(error), _) | (_, Err(error)) => (false, UpdaterState::Failed, error),
    };
    ffi::UpdaterTransitionDto {
        allowed,
        state: updater_state_dto(next),
        error,
    }
}

pub fn grid_edit_note(note: ffi::GridEditNoteDto) -> String {
    use choscordb_core::grid_edit_machine::GridEditNote as Note;
    use ffi::GridEditNoteDto as Dto;
    let note = match note {
        Dto::PlanUnfinished => Note::PlanUnfinished,
        Dto::SubmitFailed => Note::SubmitFailed,
        Dto::WorkspaceClosed => Note::WorkspaceClosed,
        _ => return format!("Unknown grid edit note {}.", note.repr),
    };
    grid_edit_machine::note(note).to_owned()
}

pub fn ssh_trust_note(note: ffi::SshTrustNoteDto) -> String {
    use choscordb_core::profile_test_machine::SshTrustNote as Note;
    use ffi::SshTrustNoteDto as Dto;
    let note = match note {
        Dto::Inspecting => Note::Inspecting,
        Dto::SettingsChanged => Note::SettingsChanged,
        Dto::NoHostKeys => Note::NoHostKeys,
        Dto::KeysInspected => Note::KeysInspected,
        Dto::ApprovalUnknown => Note::ApprovalUnknown,
        Dto::ApprovalStale => Note::ApprovalStale,
        _ => return format!("Unknown SSH trust note {}.", note.repr),
    };
    choscordb_core::profile_test_machine::ssh_trust_note(note)
}

pub fn profile_operation_presentation(
    operation: ffi::ProfileOperationDto,
    detail: &str,
) -> ffi::PresentationDto {
    use choscordb_core::profile_test_machine::{ProfileOperation as Operation, present_operation};
    use ffi::ProfileOperationDto as Dto;
    let operation = match operation {
        Dto::Loading => Operation::Loading,
        Dto::Saving => Operation::Saving,
        Dto::Duplicating => Operation::Duplicating,
        Dto::Deleting => Operation::Deleting,
        Dto::Connecting => Operation::Connecting,
        Dto::Listed => Operation::Listed,
        Dto::Saved => Operation::Saved,
        Dto::Deleted => Operation::Deleted,
        Dto::Connected => Operation::Connected,
        Dto::Failed => Operation::Failed,
        Dto::ConnectUnsubmitted => Operation::ConnectUnsubmitted,
        Dto::ConnectionClosed => Operation::ConnectionClosed,
        Dto::ProfileMissing => Operation::ProfileMissing,
        Dto::ListRefreshFailed => Operation::ListRefreshFailed,
        _ => return invalid(format!("Unknown profile operation {}.", operation.repr)),
    };
    presentation(present_operation(operation, detail))
}
