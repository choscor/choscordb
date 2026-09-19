use choscordb_driver_api::*;
use std::time::Duration;
#[derive(Clone, Debug)]
pub struct EngineConfig {
    pub storage_path: Option<std::path::PathBuf>,
    pub page_memory: crate::PageMemoryConfig,
    pub command_capacity: usize,
    pub event_capacity: usize,
    pub max_connections: usize,
    pub max_queries: usize,
    pub cancellation_grace: Duration,
    pub result_store_directory: Option<std::path::PathBuf>,
    pub result_store: choscordb_result_store::StoreConfig,
}
impl Default for EngineConfig {
    fn default() -> Self {
        Self {
            storage_path: None,
            page_memory: Default::default(),
            command_capacity: 64,
            event_capacity: 128,
            max_connections: 32,
            max_queries: 256,
            cancellation_grace: Duration::from_secs(2),
            result_store: Default::default(),
            result_store_directory: None,
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, thiserror::Error)]
pub enum SubmitError {
    #[error("invalid command input")]
    InvalidInput,
    #[error("command queue is full")]
    QueueFull,
    #[error("handle is stale")]
    StaleHandle,
    #[error("connection is disconnected")]
    Disconnected,
    #[error("driver is not registered")]
    UnknownDriver,
    #[error("engine resource limit reached")]
    ResourceLimit,
    #[error("engine is shutting down")]
    ShuttingDown,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum QueryState {
    Queued,
    Running,
    Cancelling,
    Completed,
    Failed,
    Disconnected,
}
pub type ExportId = Handle;
pub use choscordb_export::{ExportFormat, SqlDialect};

pub enum Event {
    AppearanceLayout {
        request_token: u64,
        appearance: Option<crate::AppearanceLayout>,
    },
    QueryPreferences {
        request_token: u64,
        preferences: crate::QueryPreferences,
    },
    EditorPreferences {
        request_token: u64,
        preferences: crate::EditorPreferences,
    },
    HistoryFlushed {
        request_token: u64,
    },
    HistoryWriteFailed {
        query: QueryId,
        error: DriverError,
    },
    WorkspaceSaved {
        request_token: u64,
    },
    WorkspaceRestored {
        request_token: u64,
        documents: Vec<crate::EditorDocument>,
    },
    HistoryListed {
        request_token: u64,
        entries: Vec<crate::HistoryEntry>,
    },
    HistoryCleared {
        request_token: u64,
    },
    HistoryPolicy {
        request_token: u64,
        policy: crate::HistoryPolicy,
    },
    HistoryRecorded {
        request_token: u64,
        recorded: bool,
    },
    RecoveryFailed {
        request_token: u64,
        error: DriverError,
    },
    Profiles {
        request_token: u64,
        profiles: Vec<crate::ConnectionProfile>,
    },
    ProfileSaved {
        warning: Option<String>,
        request_token: u64,
        profile: crate::ConnectionProfile,
    },
    ProfileDeleted {
        warning: Option<String>,
        request_token: u64,
        id: String,
    },
    ProfileTested {
        request_token: u64,
    },
    ProfileFailed {
        request_token: u64,
        error: DriverError,
    },
    ExportProgress {
        export: ExportId,
        query: QueryId,
        rows: u64,
        bytes: u64,
    },
    ExportFinished {
        export: ExportId,
        query: QueryId,
        rows: u64,
        bytes: u64,
    },
    ExportFailed {
        export: ExportId,
        query: QueryId,
        error: DriverError,
    },
    ValueChunk {
        query: QueryId,
        handle: Handle,
        chunk: ValueChunk,
        lease: crate::PageLease,
    },
    ValueChunkFailed {
        query: QueryId,
        handle: Handle,
        offset: u64,
        error: DriverError,
    },
    Value {
        query: QueryId,
        handle: Handle,
        value: Value,
    },
    Ddl {
        connection: ConnectionId,
        object: ObjectId,
        ddl: String,
        request_token: u64,
    },
    DdlFailed {
        connection: ConnectionId,
        object: ObjectId,
        request_token: u64,
        error: DriverError,
    },
    Connected {
        connection: ConnectionId,
        capabilities: DriverCapabilities,
    },
    Disconnected {
        connection: ConnectionId,
    },
    ConnectionFailed {
        connection: ConnectionId,
        error: DriverError,
    },
    QueryState {
        query: QueryId,
        state: QueryState,
    },
    Schema {
        query: QueryId,
        columns: Vec<Column>,
        lease: crate::PageLease,
    },
    StoredPage {
        query: QueryId,
        first_row: u64,
        page: ResultPage,
        lease: crate::PageLease,
    },
    Page {
        query: QueryId,
        page: ResultPage,
        lease: crate::PageLease,
    },
    QueryFinished {
        query: QueryId,
        duration: Duration,
        summary: QuerySummary,
    },
    QueryFailed {
        query: QueryId,
        error: DriverError,
    },
    Metadata {
        connection: ConnectionId,
        parent: Option<ObjectId>,
        request_token: u64,
        objects: Vec<SchemaObject>,
    },
    MetadataFailed {
        connection: ConnectionId,
        parent: Option<ObjectId>,
        request_token: u64,
        error: DriverError,
    },
    TransactionFinished {
        connection: ConnectionId,
        committed: bool,
    },
    OperationFailed {
        connection: ConnectionId,
        error: DriverError,
    },
}

impl std::fmt::Debug for Event {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Event")
            .field("kind", &std::mem::discriminant(self))
            .finish_non_exhaustive()
    }
}
