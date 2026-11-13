use choscordb_driver_api::{
    Body, ErrorKind, Pane, Presentation, RecoveryHint, Tone, TransitionError, detail_or,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum QueryState {
    Idle,
    Connecting,
    Queued,
    Running,
    Cancelling,
    Completed { empty: bool },
    Failed,
    Cancelled,
    Disconnected,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum QueryEvent {
    Connect,
    SessionReady,
    ConnectFailed,
    Submit,
    Start,
    Fetch,
    Cancel,
    TimedOut,
    Finish { empty: bool },
    Fail,
    CancelSettled,
    Disconnect,
    Reset,
}

impl QueryState {
    pub const ALL: [Self; 10] = [
        Self::Idle,
        Self::Connecting,
        Self::Queued,
        Self::Running,
        Self::Cancelling,
        Self::Completed { empty: false },
        Self::Completed { empty: true },
        Self::Failed,
        Self::Cancelled,
        Self::Disconnected,
    ];
}

impl QueryEvent {
    pub const ALL: [Self; 14] = [
        Self::Connect,
        Self::SessionReady,
        Self::ConnectFailed,
        Self::Submit,
        Self::Start,
        Self::Fetch,
        Self::Cancel,
        Self::TimedOut,
        Self::Finish { empty: false },
        Self::Finish { empty: true },
        Self::Fail,
        Self::CancelSettled,
        Self::Disconnect,
        Self::Reset,
    ];
}

pub type QueryTransitionError = TransitionError<QueryState, QueryEvent>;

pub fn step(state: &QueryState, event: &QueryEvent) -> Result<QueryState, QueryTransitionError> {
    use QueryEvent as E;
    use QueryState as S;
    let settled = matches!(
        state,
        S::Completed { .. } | S::Failed | S::Cancelled | S::Disconnected
    );
    let restartable = settled || *state == S::Idle;
    let working = matches!(state, S::Queued | S::Running | S::Cancelling);
    let next = match (*state, *event) {
        (_, E::Connect) if restartable => S::Connecting,
        (S::Connecting, E::SessionReady) => S::Idle,
        (S::Connecting, E::ConnectFailed) => S::Failed,
        (_, E::Submit) if restartable && *state != S::Disconnected => S::Queued,
        (S::Queued, E::Start) => S::Running,
        (S::Completed { .. }, E::Fetch) => S::Running,
        (_, E::Cancel | E::TimedOut) if working => S::Cancelling,
        (S::Failed, E::Cancel) => S::Failed,
        (S::Running, E::Finish { empty }) => S::Completed { empty },
        (S::Idle | S::Failed | S::Completed { .. }, E::Fail) => S::Failed,
        (_, E::Fail) if working => S::Failed,
        (S::Cancelling, E::CancelSettled) => S::Cancelled,
        (S::Failed, E::Disconnect) => S::Failed,
        (_, E::Disconnect) => S::Disconnected,
        (_, E::Reset) if restartable => S::Idle,
        _ => {
            return Err(TransitionError {
                state: *state,
                event: *event,
            });
        }
    };
    Ok(next)
}

pub fn failure_path(kind: ErrorKind) -> &'static [QueryState] {
    match kind {
        ErrorKind::Cancelled => &[QueryState::Cancelling, QueryState::Cancelled],
        ErrorKind::Timeout => &[QueryState::Cancelling, QueryState::Failed],
        _ => &[QueryState::Failed],
    }
}

pub fn present(state: &QueryState, detail: &str) -> Presentation {
    use QueryState as S;
    match state {
        S::Idle => Presentation::idle(detail_or(detail, "Run a query to see results.")),
        S::Connecting => Presentation::busy(detail_or(detail, "Connecting…")),
        S::Queued => Presentation::busy(detail_or(detail, "Queued")).with_pane(Pane::Grid),
        S::Running => Presentation::busy(detail_or(detail, "Running"))
            .with_body(Body::Content)
            .with_pane(Pane::Grid),
        S::Cancelling => {
            Presentation::busy(detail_or(detail, "Cancelling…")).with_body(Body::Content)
        }
        S::Completed { empty: false } => Presentation::idle(detail_or(detail, "Completed"))
            .with_tone(Tone::Success)
            .with_body(Body::Content)
            .with_pane(Pane::Grid),
        S::Completed { empty: true } => {
            Presentation::idle(detail_or(detail, "Completed. The query returned no rows."))
                .with_tone(Tone::Success)
                .with_body(Body::Empty)
                .with_pane(Pane::Grid)
        }
        S::Failed => Presentation {
            body: Body::None,
            pane: Pane::Messages,
            ..Presentation::failed("Failed", detail, RecoveryHint::RetryQuery)
        },
        S::Cancelled => Presentation::idle(detail_or(detail, "Cancelled")),
        S::Disconnected => Presentation::idle(detail_or(
            detail,
            "Disconnected. Connect and run a query to see results.",
        )),
    }
}
