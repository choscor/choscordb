use choscordb_driver_api::TransitionError;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ConnectionState {
    Connecting,
    Probing,
    Connected { transaction_active: bool },
    Failed,
    Disconnecting,
    Disconnected,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ConnectionEvent {
    Opened,
    OpenFailed,
    Probed { transaction_active: bool },
    ProbeFailed,
    TransactionChanged { active: bool },
    Shutdown,
    Closed,
}

impl ConnectionState {
    pub const ALL: [Self; 7] = [
        Self::Connecting,
        Self::Probing,
        Self::Connected {
            transaction_active: false,
        },
        Self::Connected {
            transaction_active: true,
        },
        Self::Failed,
        Self::Disconnecting,
        Self::Disconnected,
    ];
}

impl ConnectionEvent {
    pub const ALL: [Self; 9] = [
        Self::Opened,
        Self::OpenFailed,
        Self::Probed {
            transaction_active: false,
        },
        Self::Probed {
            transaction_active: true,
        },
        Self::ProbeFailed,
        Self::TransactionChanged { active: false },
        Self::TransactionChanged { active: true },
        Self::Shutdown,
        Self::Closed,
    ];
}

pub type ConnectionTransitionError = TransitionError<ConnectionState, ConnectionEvent>;

pub fn step(
    state: &ConnectionState,
    event: &ConnectionEvent,
) -> Result<ConnectionState, ConnectionTransitionError> {
    use ConnectionEvent as E;
    use ConnectionState as S;
    match (*state, *event) {
        (S::Connecting, E::Opened) => Ok(S::Probing),
        (S::Connecting, E::OpenFailed) | (S::Probing, E::ProbeFailed) => Ok(S::Failed),
        (S::Probing, E::Probed { transaction_active }) => Ok(S::Connected { transaction_active }),
        (S::Connecting | S::Probing, E::Shutdown) => Ok(S::Disconnected),
        (S::Connected { .. }, E::TransactionChanged { active }) => Ok(S::Connected {
            transaction_active: active,
        }),
        (S::Connected { .. }, E::Shutdown) => Ok(S::Disconnecting),
        (S::Disconnecting, E::Closed) => Ok(S::Disconnected),
        _ => Err(TransitionError {
            state: *state,
            event: *event,
        }),
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CatalogSession {
    Unsupported,
    Closed,
    Open,
    RetryPending,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReadSession {
    Query,
    Catalog,
}

impl CatalogSession {
    pub const ALL: [Self; 4] = [
        Self::Unsupported,
        Self::Closed,
        Self::Open,
        Self::RetryPending,
    ];
}

pub fn read_session(transaction_active: bool, catalog: CatalogSession) -> ReadSession {
    match catalog {
        _ if transaction_active => ReadSession::Query,
        CatalogSession::Unsupported | CatalogSession::RetryPending => ReadSession::Query,
        CatalogSession::Closed | CatalogSession::Open => ReadSession::Catalog,
    }
}
