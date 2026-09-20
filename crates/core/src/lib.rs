//! Nonblocking application boundary. Database work belongs to bounded per-connection
//! actors; dropping the engine never joins workers on the caller's thread.
mod actor;
mod deferred;
mod hot_cache;
pub use hot_cache::CacheUsage;
mod memory;
pub use memory::{MemoryUsage, PageLease, PageMemoryConfig};
mod operation;
mod profiles;
mod query_history;
mod recovery;
pub use profiles::CredentialUpdate;
mod protocol;
pub use choscordb_storage::{
    APPEARANCE_LAYOUT_VERSION, Accent, AccentPreset, AppearanceLayout, ConnectionProfile,
    DEFAULT_EDITOR_FONT_SIZE, Density, EDITOR_PREFERENCES_VERSION, EditorDocument,
    EditorPreferences, HistoryEntry, HistoryPolicy, HistoryStatus, MAX_APPEARANCE_LAYOUT_BYTES,
    MAX_COLLECTION_BYTES, MAX_EDITOR_FONT_SIZE, MAX_FONT_FAMILY_BYTES, MAX_QUERY_TIMEOUT_SECONDS,
    MAX_SCREEN_NAME_BYTES, MAX_SHORTCUT_SEQUENCE_BYTES, MAX_SQL_BYTES, MAX_WINDOW_DIMENSION,
    MAX_WORKSPACE_DOCUMENTS, MIN_EDITOR_FONT_SIZE, MIN_WINDOW_HEIGHT, MIN_WINDOW_WIDTH, ObjectTab,
    PostgresTls, ProfileConfiguration, QUERY_PREFERENCES_VERSION, QueryPreferences,
    ShortcutCommand, ShortcutOverride, ThemeMode, WindowGeometry, WorkspaceLayout,
    WorkspaceSnapshot, WorkspaceTab,
};
mod store;
use choscordb_driver_api::*;
pub use protocol::*;
use std::{collections::HashMap, sync::Arc};
use tokio::{
    runtime::Runtime,
    sync::{mpsc, watch},
};

struct ConnectionSlot {
    commands: mpsc::Sender<actor::Command>,
    disconnect: watch::Sender<bool>,
}
struct ExportSlot {
    connection: ConnectionId,
    cancellation: choscordb_export::Cancellation,
}
struct QuerySlot {
    connection: ConnectionId,
    cancellation: watch::Sender<bool>,
    released: watch::Sender<bool>,
}

pub struct Engine {
    history_memory: Arc<std::sync::atomic::AtomicUsize>,
    recovery_pending: std::sync::atomic::AtomicBool,
    profiles: mpsc::Sender<profiles::Command>,
    profile_tests: Arc<tokio::sync::Semaphore>,
    runtime: Option<Runtime>,
    drivers: HashMap<&'static str, Arc<dyn DatabaseDriver>>,
    connections: Arena<ConnectionSlot>,
    queries: Arena<QuerySlot>,
    connection_count: usize,
    query_count: usize,
    query_owners: HashMap<QueryId, ConnectionId>,
    exports: Arena<ExportSlot>,
    export_owners: HashMap<ConnectionId, ExportId>,
    events_tx: mpsc::Sender<Event>,
    events: mpsc::Receiver<Event>,
    shutdown: watch::Sender<bool>,
    config: EngineConfig,
    memory: Arc<memory::Memory>,
    cache: Arc<hot_cache::HotCache>,
}
impl Engine {
    /// Construct during application initialization. Subsequent commands never wait for I/O.
    pub fn new(
        config: EngineConfig,
        drivers: Vec<Arc<dyn DatabaseDriver>>,
    ) -> std::io::Result<Self> {
        Self::new_with_credentials(
            config,
            drivers,
            Arc::new(choscordb_credentials::UnavailableStore),
        )
    }
    pub fn new_with_credentials(
        config: EngineConfig,
        drivers: Vec<Arc<dyn DatabaseDriver>>,
        credentials: Arc<dyn choscordb_credentials::CredentialStore>,
    ) -> std::io::Result<Self> {
        if config.command_capacity == 0
            || config.event_capacity == 0
            || config.max_connections == 0
            || config.max_queries == 0
        {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "engine capacities must be positive",
            ));
        }
        let memory = memory::Memory::new(config.page_memory.clone())?;
        let cache = hot_cache::HotCache::new(&config.page_memory);
        memory.set_cache(&cache);
        let runtime = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .enable_all()
            .build()?;
        let (events_tx, events) = mpsc::channel(config.event_capacity);
        let (shutdown, _) = watch::channel(false);
        let mut registry = HashMap::new();
        for driver in drivers {
            if registry.insert(driver.id(), driver).is_some() {
                runtime.shutdown_background();
                return Err(std::io::Error::new(
                    std::io::ErrorKind::InvalidInput,
                    "duplicate driver identifier",
                ));
            }
        }
        let profiles = profiles::start(
            config.storage_path.clone(),
            config.command_capacity,
            events_tx.clone(),
            shutdown.subscribe(),
            runtime.handle().clone(),
            credentials,
        )?;
        Ok(Self {
            history_memory: Arc::new(std::sync::atomic::AtomicUsize::new(0)),
            recovery_pending: std::sync::atomic::AtomicBool::new(false),
            profiles,
            profile_tests: Arc::new(tokio::sync::Semaphore::new(config.max_connections)),
            runtime: Some(runtime),
            drivers: registry,
            connections: Arena::default(),
            queries: Arena::default(),
            connection_count: 0,
            query_count: 0,
            query_owners: HashMap::new(),
            exports: Arena::default(),
            export_owners: HashMap::new(),
            events_tx,
            events,
            shutdown,
            config,
            memory,
            cache,
        })
    }
    pub fn connect(
        &mut self,
        driver: &str,
        options: ConnectionOptions,
    ) -> std::result::Result<ConnectionId, SubmitError> {
        self.ensure_running()?;
        if self.connection_count >= self.config.max_connections {
            return Err(SubmitError::ResourceLimit);
        }
        let driver = self
            .drivers
            .get(driver)
            .cloned()
            .ok_or(SubmitError::UnknownDriver)?;
        self.connect_driver(driver, options)
    }
    fn connect_driver(
        &mut self,
        driver: Arc<dyn DatabaseDriver>,
        options: ConnectionOptions,
    ) -> std::result::Result<ConnectionId, SubmitError> {
        self.ensure_running()?;
        if self.connection_count >= self.config.max_connections {
            return Err(SubmitError::ResourceLimit);
        }
        let (tx, rx) = mpsc::channel(self.config.command_capacity);
        let (disconnect, disconnected) = watch::channel(false);
        let mut global_shutdown = self.shutdown.subscribe();
        let stop_connection = disconnect.clone();
        self.runtime
            .as_ref()
            .ok_or(SubmitError::ShuttingDown)?
            .spawn(async move {
                tokio::select! {
                    _ = global_shutdown.changed() => { stop_connection.send_replace(true); }
                    _ = stop_connection.closed() => {}
                }
            });
        let id = self.connections.insert(ConnectionSlot {
            commands: tx,
            disconnect,
        });
        self.connection_count += 1;
        self.runtime
            .as_ref()
            .ok_or(SubmitError::ShuttingDown)?
            .spawn(actor::run(
                id,
                driver,
                options,
                rx,
                self.events_tx.clone(),
                disconnected,
                self.config.cancellation_grace,
                self.config.result_store.clone(),
                self.config.result_store_directory.clone(),
                self.memory.clone(),
                self.cache.clone(),
            ));
        Ok(id)
    }
    pub fn execute(
        &mut self,
        connection: ConnectionId,
        sql: String,
        options: QueryOptions,
    ) -> std::result::Result<QueryId, SubmitError> {
        self.execute_with_profile(connection, sql, options, None)
    }
    pub fn open_object_data(
        &mut self,
        connection: ConnectionId,
        object: ObjectId,
        options: QueryOptions,
    ) -> std::result::Result<QueryId, SubmitError> {
        if object.0.is_empty() || object.0.len() > 16384 {
            return Err(SubmitError::InvalidInput);
        }
        self.execute_source(connection, String::new(), options, None, Some(object))
    }
    pub fn execute_with_profile(
        &mut self,
        connection: ConnectionId,
        sql: String,
        options: QueryOptions,
        profile_id: Option<String>,
    ) -> std::result::Result<QueryId, SubmitError> {
        self.execute_source(connection, sql, options, profile_id, None)
    }
    fn execute_source(
        &mut self,
        connection: ConnectionId,
        sql: String,
        options: QueryOptions,
        profile_id: Option<String>,
        object: Option<ObjectId>,
    ) -> std::result::Result<QueryId, SubmitError> {
        self.ensure_running()?;
        if profile_id
            .as_ref()
            .is_some_and(|id| id.is_empty() || id.len() > 256 || id.contains('\0'))
        {
            return Err(SubmitError::InvalidInput);
        }
        if self.query_count >= self.config.max_queries {
            return Err(SubmitError::ResourceLimit);
        }
        let tx = self
            .connections
            .get(connection)
            .ok_or(SubmitError::StaleHandle)?
            .commands
            .clone();
        let command_permit = tx.try_reserve().map_err(map_send)?;
        let event_sender = self.events_tx.clone();
        let event_permit = event_sender.try_reserve().map_err(map_send)?;
        let (cancellation, cancel_rx) = watch::channel(false);
        let (released, _) = watch::channel(false);
        let id = self.queries.insert(QuerySlot {
            connection,
            cancellation,
            released,
        });
        event_permit.send(Event::QueryState {
            query: id,
            state: QueryState::Queued,
        });
        let sql = Arc::new(sql);
        let history = if object.is_some() {
            None
        } else {
            let _entered = self
                .runtime
                .as_ref()
                .ok_or(SubmitError::ShuttingDown)?
                .enter();
            crate::query_history::Ticket::new(
                id,
                sql.clone(),
                profile_id,
                self.history_memory.clone(),
                self.profiles.clone(),
                self.events_tx.clone(),
            )
        };
        command_permit.send(actor::Command::Execute {
            query: id,
            sql,
            object,
            history,
            options,
            cancellation: cancel_rx,
        });
        self.query_count += 1;
        self.query_owners.insert(id, connection);
        Ok(id)
    }
    /// Export the original result, from row zero, without executing SQL again.
    pub fn start_export(
        &mut self,
        query: QueryId,
        destination: std::path::PathBuf,
        format: ExportFormat,
    ) -> std::result::Result<ExportId, SubmitError> {
        self.ensure_running()?;
        let slot = self.queries.get(query).ok_or(SubmitError::StaleHandle)?;
        let connection = slot.connection;
        if self.export_owners.contains_key(&connection) {
            return Err(SubmitError::ResourceLimit);
        }
        let released = slot.released.subscribe();
        let tx = self
            .connections
            .get(connection)
            .ok_or(SubmitError::StaleHandle)?
            .commands
            .clone();
        let permit = tx.try_reserve().map_err(map_send)?;
        let cancellation = choscordb_export::Cancellation::default();
        let id = self.exports.insert(ExportSlot {
            connection,
            cancellation: cancellation.clone(),
        });
        self.export_owners.insert(connection, id);
        permit.send(actor::Command::Export {
            export: id,
            query,
            destination,
            format,
            cancellation,
            released,
        });
        Ok(id)
    }
    pub fn cancel_export(&self, export: ExportId) -> std::result::Result<(), SubmitError> {
        self.ensure_running()?;
        self.exports
            .get(export)
            .ok_or(SubmitError::StaleHandle)?
            .cancellation
            .cancel();
        Ok(())
    }
    /// An out-of-band signal, so a saturated command queue cannot prevent cancellation.
    pub fn cancel(&self, query: QueryId) -> std::result::Result<(), SubmitError> {
        self.ensure_running()?;
        let slot = self.queries.get(query).ok_or(SubmitError::StaleHandle)?;
        slot.cancellation.send_replace(true);
        Ok(())
    }
    pub fn fetch_page(
        &self,
        query: QueryId,
        size: PageSize,
    ) -> std::result::Result<(), SubmitError> {
        let owner = self
            .queries
            .get(query)
            .ok_or(SubmitError::StaleHandle)?
            .connection;
        self.submit(
            owner,
            actor::Command::Fetch {
                query,
                size,
                index: None,
                released: self
                    .queries
                    .get(query)
                    .ok_or(SubmitError::StaleHandle)?
                    .released
                    .subscribe(),
            },
        )
    }
    /// Read a persisted ordinal, or continue the original cursor at the next ordinal.
    /// Previously fetched pages and deferred readers survive a new query until release.
    pub fn fetch_page_at(
        &self,
        query: QueryId,
        index: u64,
        size: PageSize,
    ) -> std::result::Result<(), SubmitError> {
        let owner = self
            .queries
            .get(query)
            .ok_or(SubmitError::StaleHandle)?
            .connection;
        self.submit(
            owner,
            actor::Command::Fetch {
                query,
                size,
                index: Some(index),
                released: self
                    .queries
                    .get(query)
                    .ok_or(SubmitError::StaleHandle)?
                    .released
                    .subscribe(),
            },
        )
    }
    pub fn load_value(
        &self,
        query: QueryId,
        handle: Handle,
    ) -> std::result::Result<(), SubmitError> {
        let owner = self
            .queries
            .get(query)
            .ok_or(SubmitError::StaleHandle)?
            .connection;
        self.submit(owner, actor::Command::LoadValue { query, handle })
    }
    /// Read bounded original value bytes without accessing the database cursor.
    pub fn load_value_chunk(
        &self,
        query: QueryId,
        handle: Handle,
        offset: u64,
        max_bytes: usize,
    ) -> std::result::Result<(), SubmitError> {
        let slot = self.queries.get(query).ok_or(SubmitError::StaleHandle)?;
        self.submit(
            slot.connection,
            actor::Command::LoadValueChunk {
                query,
                handle,
                offset,
                max_bytes,
                released: slot.released.subscribe(),
            },
        )
    }
    pub fn object_ddl(
        &self,
        connection: ConnectionId,
        object: ObjectId,
    ) -> std::result::Result<(), SubmitError> {
        self.object_ddl_request(connection, object, 0)
    }
    pub fn object_ddl_request(
        &self,
        connection: ConnectionId,
        object: ObjectId,
        request_token: u64,
    ) -> std::result::Result<(), SubmitError> {
        self.submit(
            connection,
            actor::Command::Ddl {
                object,
                request_token,
            },
        )
    }
    pub fn apply_edit_batch(
        &self,
        connection: ConnectionId,
        batch: EditBatch,
        request_token: u64,
    ) -> std::result::Result<(), SubmitError> {
        if batch.statements.is_empty()
            || batch.statements.len() > 1000
            || batch
                .statements
                .iter()
                .any(|s| s.sql.len() > MAX_SQL_BYTES || s.params.len() > 256)
        {
            return Err(SubmitError::InvalidInput);
        }
        self.submit(
            connection,
            actor::Command::Edit {
                batch,
                request_token,
            },
        )
    }
    pub fn edit_target_request(
        &self,
        connection: ConnectionId,
        object: ObjectId,
        request_token: u64,
    ) -> std::result::Result<(), SubmitError> {
        if object.0.is_empty() || object.0.len() > 16384 {
            return Err(SubmitError::InvalidInput);
        }
        self.submit(
            connection,
            actor::Command::EditTarget {
                object,
                request_token,
            },
        )
    }
    pub fn edit_query_request(
        &self,
        connection: ConnectionId,
        sql: String,
        result_columns: Vec<String>,
        request_token: u64,
    ) -> std::result::Result<(), SubmitError> {
        if sql.is_empty()
            || sql.len() > MAX_SQL_BYTES
            || result_columns.is_empty()
            || result_columns.len() > 1000
            || result_columns.iter().any(|name| name.len() > 1024)
        {
            return Err(SubmitError::InvalidInput);
        }
        self.submit(
            connection,
            actor::Command::EditQuery {
                sql,
                result_columns,
                request_token,
            },
        )
    }
    pub fn load_metadata(
        &self,
        connection: ConnectionId,
        parent: Option<ObjectId>,
    ) -> std::result::Result<(), SubmitError> {
        self.load_metadata_request(connection, parent, 0)
    }
    /// Echoes the caller's request token, allowing a navigator to discard stale refreshes.
    pub fn load_metadata_request(
        &self,
        connection: ConnectionId,
        parent: Option<ObjectId>,
        request_token: u64,
    ) -> std::result::Result<(), SubmitError> {
        self.submit(
            connection,
            actor::Command::Metadata {
                parent,
                request_token,
            },
        )
    }
    pub fn commit(&self, connection: ConnectionId) -> std::result::Result<(), SubmitError> {
        self.submit(connection, actor::Command::Transaction(true))
    }
    pub fn rollback(&self, connection: ConnectionId) -> std::result::Result<(), SubmitError> {
        self.submit(connection, actor::Command::Transaction(false))
    }
    pub fn release_query(&mut self, query: QueryId) -> std::result::Result<(), SubmitError> {
        let owner = self
            .queries
            .get(query)
            .ok_or(SubmitError::StaleHandle)?
            .connection;
        self.queries
            .get(query)
            .ok_or(SubmitError::StaleHandle)?
            .cancellation
            .send_replace(true);
        self.queries
            .get(query)
            .ok_or(SubmitError::StaleHandle)?
            .released
            .send_replace(true);
        self.submit(owner, actor::Command::Release(query))?;
        self.queries.remove(query);
        self.query_owners.remove(&query);
        self.query_count -= 1;
        Ok(())
    }
    /// Release the connection handle after the disconnect command has been accepted.
    pub fn disconnect(&mut self, connection: ConnectionId) -> std::result::Result<(), SubmitError> {
        self.ensure_running()?;
        self.connections
            .get(connection)
            .ok_or(SubmitError::StaleHandle)?
            .disconnect
            .send_replace(true);
        self.connections.remove(connection);
        self.connection_count -= 1;
        Ok(())
    }
    pub fn cache_usage(&self) -> CacheUsage {
        self.cache.usage()
    }
    pub fn memory_usage(&self) -> MemoryUsage {
        self.memory.usage()
    }
    pub fn try_event(&mut self) -> Option<Event> {
        let event = self.events.try_recv().ok()?;
        if matches!(
            &event,
            Event::QueryPreferences { .. }
                | Event::EditorPreferences { .. }
                | Event::AppearanceLayout { .. }
                | Event::HistoryFlushed { .. }
                | Event::WorkspaceSaved { .. }
                | Event::WorkspaceRestored { .. }
                | Event::WorkspaceTabsRestored { .. }
                | Event::HistoryListed { .. }
                | Event::HistoryCleared { .. }
                | Event::HistoryPolicy { .. }
                | Event::HistoryRecorded { .. }
                | Event::RecoveryFailed { .. }
        ) {
            self.recovery_pending
                .store(false, std::sync::atomic::Ordering::Release);
        }
        if let Event::ExportFinished { export, .. } | Event::ExportFailed { export, .. } = &event
            && let Some(slot) = self.exports.remove(*export)
        {
            self.export_owners.remove(&slot.connection);
        }

        if let Event::Disconnected { connection } | Event::ConnectionFailed { connection, .. } =
            &event
        {
            if self.connections.remove(*connection).is_some() {
                self.connection_count -= 1;
            }
            if let Some(export) = self.export_owners.remove(connection)
                && let Some(slot) = self.exports.remove(export)
            {
                slot.cancellation.cancel();
            }
            let ids: Vec<_> = self
                .query_owners
                .iter()
                .filter(|(_, owner)| **owner == *connection)
                .map(|(id, _)| *id)
                .collect();
            for id in ids {
                self.queries.remove(id);
                self.query_owners.remove(&id);
                self.query_count -= 1;
            }
        }
        Some(event)
    }
    /// Starts cooperative cancellation. Continue draining events during shutdown.
    pub fn initiate_shutdown(&self) {
        self.shutdown.send_replace(true);
    }
    fn ensure_running(&self) -> std::result::Result<(), SubmitError> {
        if *self.shutdown.borrow() {
            Err(SubmitError::ShuttingDown)
        } else {
            Ok(())
        }
    }
    fn submit(
        &self,
        connection: ConnectionId,
        command: actor::Command,
    ) -> std::result::Result<(), SubmitError> {
        self.ensure_running()?;
        self.connections
            .get(connection)
            .ok_or(SubmitError::StaleHandle)?
            .commands
            .try_send(command)
            .map_err(map_send)
    }
}
fn map_send<T>(error: mpsc::error::TrySendError<T>) -> SubmitError {
    match error {
        mpsc::error::TrySendError::Full(_) => SubmitError::QueueFull,
        mpsc::error::TrySendError::Closed(_) => SubmitError::Disconnected,
    }
}
impl Drop for Engine {
    fn drop(&mut self) {
        self.initiate_shutdown();
        if let Some(runtime) = self.runtime.take() {
            runtime.shutdown_background();
        }
    }
}
