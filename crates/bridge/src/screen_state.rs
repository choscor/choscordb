use crate::ffi;
use choscordb_core::{
    export_machine::{self, ExportEvent, ExportState},
    panel_machine::{self, PanelEvent, PanelKind, PanelState},
    profile_test_machine::{self, ProfileTestState},
    query_machine::{self, QueryEvent, QueryState},
};
use choscordb_driver_api::{Body, Pane, Presentation, RetryAction, Tone, TransitionError};
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
    let (has_toast, toast) = match shown.toast {
        Some(toast) => (true, toast.text),
        None => (false, String::new()),
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

pub fn profile_test_presentation(
    state: ffi::ProfileTestStateDto,
    detail: &str,
) -> ffi::PresentationDto {
    match profile_test_state(state) {
        Ok(state) => presentation(profile_test_machine::present(&state, detail)),
        Err(error) => invalid(error),
    }
}
