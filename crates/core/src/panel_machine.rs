use choscordb_driver_api::{Body, Presentation, RecoveryHint, Tone, TransitionError, detail_or};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PanelKind {
    SavedSql,
    History,
    Pins,
    Objects,
    ObjectDetails,
    Diagram,
    QuickSearch,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PanelState {
    Idle,
    Loading,
    Ready,
    Empty,
    NoMatch,
    Failed,
    Disconnected,
    Incomplete,
    Unsupported,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PanelEvent {
    Load,
    LoadedRows,
    LoadedNone,
    LoadedPartial,
    LoadUnsupported,
    LoadFailed,
    FilterMatched,
    FilterMissed,
    Disconnect,
}

impl PanelKind {
    pub const ALL: [Self; 7] = [
        Self::SavedSql,
        Self::History,
        Self::Pins,
        Self::Objects,
        Self::ObjectDetails,
        Self::Diagram,
        Self::QuickSearch,
    ];
}

impl PanelState {
    pub const ALL: [Self; 9] = [
        Self::Idle,
        Self::Loading,
        Self::Ready,
        Self::Empty,
        Self::NoMatch,
        Self::Failed,
        Self::Disconnected,
        Self::Incomplete,
        Self::Unsupported,
    ];
}

impl PanelEvent {
    pub const ALL: [Self; 9] = [
        Self::Load,
        Self::LoadedRows,
        Self::LoadedNone,
        Self::LoadedPartial,
        Self::LoadUnsupported,
        Self::LoadFailed,
        Self::FilterMatched,
        Self::FilterMissed,
        Self::Disconnect,
    ];
}

pub type PanelTransitionError = TransitionError<PanelState, PanelEvent>;

pub fn step(state: &PanelState, event: &PanelEvent) -> Result<PanelState, PanelTransitionError> {
    use PanelEvent as E;
    use PanelState as S;
    match (*state, *event) {
        (_, E::Load) => Ok(S::Loading),
        (S::Loading, E::LoadedRows) => Ok(S::Ready),
        (S::Loading, E::LoadedNone) => Ok(S::Empty),
        (S::Loading, E::LoadedPartial) => Ok(S::Incomplete),
        (S::Loading, E::LoadUnsupported) => Ok(S::Unsupported),
        (S::Loading, E::LoadFailed) => Ok(S::Failed),
        (S::NoMatch, E::FilterMatched) => Ok(S::Ready),
        (S::Ready, E::FilterMissed) => Ok(S::NoMatch),
        (state, E::FilterMatched | E::FilterMissed) => Ok(state),
        (S::Failed, E::Disconnect) => Ok(S::Failed),
        (_, E::Disconnect) => Ok(S::Disconnected),
        _ => Err(TransitionError {
            state: *state,
            event: *event,
        }),
    }
}

struct Texts {
    subject: &'static str,
    loading: &'static str,
    empty: &'static str,
    no_match: &'static str,
    disconnected: &'static str,
    hint: RecoveryHint,
}

fn texts(kind: PanelKind) -> Texts {
    const FILTER: &str = "Try a different filter or clear the search.";
    match kind {
        PanelKind::SavedSql => Texts {
            subject: "saved queries",
            loading: "Loading…",
            empty: "No saved queries yet.\nSave a query as a SQL file in the default folder to find it here.",
            no_match: "No matching saved queries.\nTry a different filter or clear the search.",
            disconnected: "No saved queries yet.\nSave a query as a SQL file in the default folder to find it here.",
            hint: RecoveryHint::Refresh,
        },
        PanelKind::History => Texts {
            subject: "query history",
            loading: "Loading…",
            empty: "No query history yet.\nRun a query to see it here.",
            no_match: "No matching query history.\nTry a different filter or clear the search.",
            disconnected: "No query history yet.\nRun a query to see it here.",
            hint: RecoveryHint::RetryFromHistory,
        },
        PanelKind::Pins => Texts {
            subject: "pinned objects",
            loading: "Loading…",
            empty: "No pinned objects yet.\nPin a schema or object from its menu.",
            no_match: "No matching pinned objects.\nTry a different filter or clear the search.",
            disconnected: "No pinned objects yet.\nPin a schema or object from its menu.",
            hint: RecoveryHint::ActivatePin,
        },
        PanelKind::Objects => Texts {
            subject: "objects",
            loading: "Loading…",
            empty: "No objects to show.\nRefresh to check for schemas and objects.",
            no_match: "No matching objects.\nTry a different filter or clear the search.",
            disconnected: "No database selected.\nSelect a connection to browse its schemas and objects.",
            hint: RecoveryHint::RefreshNavigator,
        },
        PanelKind::ObjectDetails => Texts {
            subject: "object details",
            loading: "Loading…",
            empty: "No details for this object.",
            no_match: FILTER,
            disconnected: "Disconnected. Reconnect to load object details.",
            hint: RecoveryHint::ReconnectAndRefresh,
        },
        PanelKind::Diagram => Texts {
            subject: "the diagram",
            loading: "Loading…",
            empty: "No foreign-key relationships.",
            no_match: FILTER,
            disconnected: "Disconnected. Reconnect to load the diagram.",
            hint: RecoveryHint::ReconnectAndRefresh,
        },
        PanelKind::QuickSearch => Texts {
            subject: "search results",
            loading: "Searching…",
            empty: "No results. Try another search.",
            no_match: "No results. Try another search.",
            disconnected: "Connect to search database objects.",
            hint: RecoveryHint::RefineOrRetry,
        },
    }
}

pub fn present(kind: PanelKind, state: &PanelState, detail: &str) -> Presentation {
    use PanelState as S;
    let texts = texts(kind);
    match state {
        S::Idle => Presentation::idle(detail.trim()),
        S::Loading => Presentation::busy(detail_or(detail, texts.loading)),
        S::Ready => Presentation::idle(detail.trim())
            .with_tone(Tone::Success)
            .with_body(Body::Content),
        S::Empty => Presentation::idle(detail_or(detail, texts.empty))
            .with_tone(Tone::Success)
            .with_body(Body::Empty),
        S::NoMatch => Presentation::idle(texts.no_match).with_body(Body::NoMatch),
        S::Failed => Presentation::failed(
            &format!("Could not load {}", texts.subject),
            detail,
            texts.hint,
        )
        .with_error_toast(),
        S::Disconnected => {
            Presentation::idle(detail_or(detail, texts.disconnected)).with_body(Body::Empty)
        }
        S::Incomplete => Presentation::idle(detail_or(detail, "Some content is unavailable."))
            .with_body(Body::Content),
        S::Unsupported => Presentation::idle(detail_or(
            detail,
            "This connection does not support this view.",
        ))
        .with_body(Body::Empty),
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PanelWork {
    Loading,
    Clearing,
    Saving,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct PanelFacts {
    pub failed: bool,
    pub work: Option<PanelWork>,
    pub loaded: bool,
    pub has_rows: bool,
    pub filtered: bool,
    pub paged: bool,
    pub disconnected: bool,
}

pub fn settle(facts: &PanelFacts) -> PanelState {
    use PanelState as S;
    if facts.failed {
        S::Failed
    } else if facts.work.is_some() {
        S::Loading
    } else if facts.disconnected {
        S::Disconnected
    } else if !facts.loaded {
        S::Idle
    } else if facts.has_rows {
        S::Ready
    } else if facts.filtered {
        S::NoMatch
    } else {
        S::Empty
    }
}

pub fn observe(kind: PanelKind, facts: &PanelFacts, error: &str) -> (PanelState, Presentation) {
    use PanelState as S;
    let state = settle(facts);
    let subject = texts(kind).subject;
    let detail = match (state, facts.work) {
        (S::Failed, _) => error.to_owned(),
        (S::Loading, Some(PanelWork::Clearing)) => format!("Clearing {subject}…"),
        (S::Loading, Some(PanelWork::Saving)) => format!("Saving the {subject} preference…"),
        (S::Empty, _) if facts.paged => format!("No {subject} on this page."),
        _ => String::new(),
    };
    (state, present(kind, &state, &detail))
}
