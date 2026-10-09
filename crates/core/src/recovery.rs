//! Inert workspace and history operations on the shared bounded metadata worker.
use crate::{
    EditorDocument, Engine, Event, HistoryEntry, HistoryPolicy, SubmitError, WorkspaceSnapshot,
};
use choscordb_driver_api::{DriverError, ErrorKind};
use choscordb_storage::{Storage, StorageError};

pub(crate) enum Command {
    AppearanceLayout(u64),
    SetAppearanceLayout(crate::AppearanceLayout, u64),
    ResetAppearanceLayout(u64),
    QueryPreferences(u64),
    SetQueryPreferences(crate::QueryPreferences, u64),
    EditorPreferences(u64),
    SetEditorPreferences(crate::EditorPreferences, u64),
    Flush(u64),
    Save(Vec<EditorDocument>, u64),
    SaveTabs(WorkspaceSnapshot, u64),
    Restore(u64),
    RestoreTabs(u64),
    List(u32, u32, u64),
    Search(String, u32, u64, u64),
    Clear(u64),
    Policy(u64),
    SetPolicy(HistoryPolicy, u64),
    Record(HistoryEntry, u64),
}
impl Command {
    pub(crate) fn token(&self) -> u64 {
        match self {
            Self::AppearanceLayout(t)
            | Self::SetAppearanceLayout(_, t)
            | Self::ResetAppearanceLayout(t)
            | Self::QueryPreferences(t)
            | Self::SetQueryPreferences(_, t)
            | Self::EditorPreferences(t)
            | Self::SetEditorPreferences(_, t)
            | Self::Flush(t)
            | Self::Save(_, t)
            | Self::SaveTabs(_, t)
            | Self::Restore(t)
            | Self::RestoreTabs(t)
            | Self::List(_, _, t)
            | Self::Search(_, _, _, t)
            | Self::Clear(t)
            | Self::Policy(t)
            | Self::SetPolicy(_, t)
            | Self::Record(_, t) => *t,
        }
    }
}
/// Requests that may wait behind the one payload the storage worker holds.
pub const MAX_WAITING_RECOVERY_REQUESTS: usize = 8;
const MAX_WAITING_RECOVERY_BYTES: usize = 2 * choscordb_storage::MAX_COLLECTION_BYTES;

/// Recovery requests waiting behind the payload held by the storage worker,
/// answered in submission order within a count and byte budget.
#[derive(Default)]
pub(crate) struct Queue {
    in_flight: bool,
    waiting: std::collections::VecDeque<(Command, usize)>,
    bytes: usize,
    /// Failures for waiting requests the worker could not accept.
    failed: std::collections::VecDeque<Event>,
}
fn invalid(error: StorageError) -> SubmitError {
    match error {
        StorageError::ResourceLimit => SubmitError::ResourceLimit,
        _ => SubmitError::InvalidInput,
    }
}
fn failure(error: StorageError) -> DriverError {
    match error {
        StorageError::StaleHistoryCursor => {
            return DriverError::new(
                ErrorKind::InvalidInput,
                "Saved history changed while searching; retry the search",
            );
        }
        StorageError::CorruptAppearance => {
            return DriverError::new(
                ErrorKind::InvalidInput,
                "Stored appearance and layout preferences are corrupt",
            );
        }
        StorageError::UnsupportedAppearanceVersion(version) => {
            return DriverError::new(
                ErrorKind::Unsupported,
                format!("Stored appearance and layout preference version {version} is unsupported"),
            );
        }
        StorageError::InvalidAppearance => {
            return DriverError::new(
                ErrorKind::InvalidInput,
                "Appearance and layout preferences are invalid",
            );
        }
        _ => {}
    }
    let kind = match error {
        StorageError::ResourceLimit => ErrorKind::ResourceLimit,
        StorageError::InvalidQueryPreferences
        | StorageError::InvalidDocument
        | StorageError::InvalidRetention
        | StorageError::InvalidPreferences => ErrorKind::InvalidInput,
        _ => ErrorKind::Io,
    };
    // Never forward SQLite/JSON diagnostics containing stored SQL or paths.
    DriverError::new(
        kind,
        "Could not complete local workspace or history operation",
    )
}
pub(crate) fn execute(storage: &mut Storage, command: Command) -> Result<Event, DriverError> {
    let request_token = command.token();
    Ok(match command {
        Command::AppearanceLayout(_) => Event::AppearanceLayout {
            request_token,
            appearance: storage.appearance_layout().map_err(failure)?,
        },
        Command::SetAppearanceLayout(appearance, _) => {
            storage
                .set_appearance_layout(&appearance)
                .map_err(failure)?;
            Event::AppearanceLayout {
                request_token,
                appearance: Some(appearance),
            }
        }
        Command::ResetAppearanceLayout(_) => {
            storage.reset_appearance_layout().map_err(failure)?;
            Event::AppearanceLayout {
                request_token,
                appearance: None,
            }
        }
        Command::QueryPreferences(_) => Event::QueryPreferences {
            request_token,
            preferences: storage.query_preferences().map_err(failure)?,
        },
        Command::SetQueryPreferences(preferences, _) => {
            storage
                .set_query_preferences(&preferences)
                .map_err(failure)?;
            Event::QueryPreferences {
                request_token,
                preferences,
            }
        }
        Command::EditorPreferences(_) => Event::EditorPreferences {
            request_token,
            preferences: storage.editor_preferences().map_err(failure)?,
        },
        Command::SetEditorPreferences(preferences, _) => {
            storage
                .set_editor_preferences(&preferences)
                .map_err(failure)?;
            Event::EditorPreferences {
                request_token,
                preferences,
            }
        }
        Command::Flush(_) => Event::HistoryFlushed { request_token },
        Command::Save(documents, _) => {
            storage.save_workspace(&documents).map_err(failure)?;
            Event::WorkspaceSaved { request_token }
        }
        Command::SaveTabs(snapshot, _) => {
            storage.save_workspace_tabs(&snapshot).map_err(failure)?;
            Event::WorkspaceSaved { request_token }
        }
        Command::Restore(_) => Event::WorkspaceRestored {
            request_token,
            documents: storage.restore_workspace().map_err(failure)?,
        },
        Command::RestoreTabs(_) => Event::WorkspaceTabsRestored {
            request_token,
            snapshot: storage.restore_workspace_tabs().map_err(failure)?,
        },
        Command::List(limit, offset, _) => Event::HistoryListed {
            request_token,
            entries: storage.history(limit, offset).map_err(failure)?,
        },
        Command::Search(query, limit, offset, _) => {
            let result = storage
                .search_history(&query, limit, offset)
                .map_err(failure)?;
            Event::HistorySearched {
                request_token,
                entries: result.entries,
                incomplete: result.incomplete,
                next_offset: result.next_offset,
            }
        }
        Command::Clear(_) => {
            storage.clear_history().map_err(failure)?;
            Event::HistoryCleared { request_token }
        }
        Command::Policy(_) => Event::HistoryPolicy {
            request_token,
            policy: storage.history_policy().map_err(failure)?,
        },
        Command::SetPolicy(policy, _) => {
            storage
                .set_history_policy(policy.clone())
                .map_err(failure)?;
            Event::HistoryPolicy {
                request_token,
                policy,
            }
        }
        Command::Record(entry, _) => {
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs()
                .min(i64::MAX as u64) as i64;
            Event::HistoryRecorded {
                request_token,
                recorded: storage.record_history(&entry, now).map_err(failure)?,
            }
        }
    })
}
impl Engine {
    pub fn appearance_layout_get(&self, token: u64) -> Result<(), SubmitError> {
        self.submit_recovery(Command::AppearanceLayout(token))
    }
    pub fn appearance_layout_set(
        &self,
        appearance: crate::AppearanceLayout,
        token: u64,
    ) -> Result<(), SubmitError> {
        self.submit_recovery(Command::SetAppearanceLayout(appearance, token))
    }
    pub fn appearance_layout_reset(&self, token: u64) -> Result<(), SubmitError> {
        self.submit_recovery(Command::ResetAppearanceLayout(token))
    }

    pub fn query_preferences_get(&self, token: u64) -> Result<(), SubmitError> {
        self.submit_recovery(Command::QueryPreferences(token))
    }
    pub fn query_preferences_set(
        &self,
        preferences: crate::QueryPreferences,
        token: u64,
    ) -> Result<(), SubmitError> {
        self.submit_recovery(Command::SetQueryPreferences(preferences, token))
    }

    pub fn editor_preferences_get(&self, token: u64) -> Result<(), SubmitError> {
        self.submit_recovery(Command::EditorPreferences(token))
    }
    pub fn editor_preferences_set(
        &self,
        preferences: crate::EditorPreferences,
        token: u64,
    ) -> Result<(), SubmitError> {
        self.submit_recovery(Command::SetEditorPreferences(preferences, token))
    }

    pub fn history_flush(&self, token: u64) -> Result<(), SubmitError> {
        self.submit_recovery(Command::Flush(token))
    }
    fn submit_recovery(&self, command: Command) -> Result<(), SubmitError> {
        self.ensure_running()?;
        let cost = match &command {
            Command::SetAppearanceLayout(appearance, _) => appearance.validate().map(|()| 0),
            Command::SetQueryPreferences(preferences, _) => preferences.validate().map(|()| 0),
            Command::SetEditorPreferences(preferences, _) => preferences.validate().map(|()| 0),
            Command::Save(documents, _) => choscordb_storage::validate_workspace(documents),
            Command::SaveTabs(snapshot, _) => choscordb_storage::validate_workspace_tabs(snapshot),
            Command::List(limit, offset, _) => {
                choscordb_storage::validate_history_page(*limit, *offset).map(|()| 0)
            }
            Command::Search(query, limit, offset, _) => {
                choscordb_storage::validate_history_search(query, *limit, *offset)
                    .map(|()| query.len())
            }
            Command::SetPolicy(policy, _) => policy.validate().map(|()| 0),
            Command::Record(entry, _) => entry.validate().map(|()| 0),
            _ => Ok(0),
        }
        .map_err(invalid)?;
        let mut queue = self.recovery_queue();
        if queue.in_flight {
            if queue.waiting.len() >= MAX_WAITING_RECOVERY_REQUESTS
                || cost > MAX_WAITING_RECOVERY_BYTES - queue.bytes
            {
                return Err(SubmitError::QueueFull);
            }
            queue.bytes += cost;
            queue.waiting.push_back((command, cost));
            return Ok(());
        }
        self.profiles
            .try_send(crate::profiles::Command::Recovery(command))
            .map_err(crate::map_send)?;
        queue.in_flight = true;
        Ok(())
    }
    fn recovery_queue(&self) -> std::sync::MutexGuard<'_, Queue> {
        self.recovery
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    }
    /// Hands the next waiting request to the worker once a response is consumed.
    pub(crate) fn advance_recovery(&self) {
        let mut queue = self.recovery_queue();
        while let Some((command, cost)) = queue.waiting.pop_front() {
            queue.bytes -= cost;
            let request_token = command.token();
            match self
                .profiles
                .try_send(crate::profiles::Command::Recovery(command))
            {
                Ok(()) => return,
                Err(error) => queue.failed.push_back(Event::RecoveryFailed {
                    request_token,
                    error: DriverError::new(
                        ErrorKind::Disconnected,
                        crate::map_send(error).to_string(),
                    ),
                }),
            }
        }
        queue.in_flight = false;
    }
    pub(crate) fn take_recovery_failure(&self) -> Option<Event> {
        self.recovery_queue().failed.pop_front()
    }
    pub fn workspace_save(
        &self,
        documents: Vec<EditorDocument>,
        token: u64,
    ) -> Result<(), SubmitError> {
        self.submit_recovery(Command::Save(documents, token))
    }
    pub fn workspace_restore(&self, token: u64) -> Result<(), SubmitError> {
        self.submit_recovery(Command::Restore(token))
    }
    pub fn workspace_tabs_save(
        &self,
        snapshot: WorkspaceSnapshot,
        token: u64,
    ) -> Result<(), SubmitError> {
        self.submit_recovery(Command::SaveTabs(snapshot, token))
    }
    pub fn workspace_tabs_restore(&self, token: u64) -> Result<(), SubmitError> {
        self.submit_recovery(Command::RestoreTabs(token))
    }
    pub fn history_list(&self, limit: u32, offset: u32, token: u64) -> Result<(), SubmitError> {
        self.submit_recovery(Command::List(limit, offset, token))
    }
    pub fn history_search(
        &self,
        query: String,
        limit: u32,
        offset: u64,
        token: u64,
    ) -> Result<(), SubmitError> {
        self.submit_recovery(Command::Search(query, limit, offset, token))
    }
    pub fn history_clear(&self, token: u64) -> Result<(), SubmitError> {
        self.submit_recovery(Command::Clear(token))
    }
    pub fn history_policy_get(&self, token: u64) -> Result<(), SubmitError> {
        self.submit_recovery(Command::Policy(token))
    }
    pub fn history_policy_set(&self, policy: HistoryPolicy, token: u64) -> Result<(), SubmitError> {
        self.submit_recovery(Command::SetPolicy(policy, token))
    }
    pub fn history_record(&self, entry: HistoryEntry, token: u64) -> Result<(), SubmitError> {
        self.submit_recovery(Command::Record(entry, token))
    }
}

/// A new identity for an editor document in workspace recovery.
pub fn new_document_id() -> String {
    uuid::Uuid::new_v4().to_string()
}

/// Which connection an object tab belongs to, as recovery stores it: a saved
/// profile, or an unsaved session connection.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ObjectTabContext {
    Profile(String),
    Session(u64),
    Unknown,
}

pub fn object_tab_context(profile_id: &str, connection: u64) -> String {
    if profile_id.is_empty() {
        format!("session:{connection}")
    } else {
        format!("profile:{profile_id}")
    }
}

pub fn parse_object_tab_context(context: &str) -> ObjectTabContext {
    if let Some(profile) = context.strip_prefix("profile:")
        && !profile.is_empty()
    {
        return ObjectTabContext::Profile(profile.into());
    }
    context
        .strip_prefix("session:")
        .and_then(|connection| connection.parse().ok())
        .map_or(ObjectTabContext::Unknown, ObjectTabContext::Session)
}
