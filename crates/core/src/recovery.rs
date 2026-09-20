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
            | Self::Clear(t)
            | Self::Policy(t)
            | Self::SetPolicy(_, t)
            | Self::Record(_, t) => *t,
        }
    }
}
fn invalid(error: StorageError) -> SubmitError {
    match error {
        StorageError::ResourceLimit => SubmitError::ResourceLimit,
        _ => SubmitError::InvalidInput,
    }
}
fn failure(error: StorageError) -> DriverError {
    match error {
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
        // One payload across both queues, until the consumer takes its response.
        self.recovery_pending
            .compare_exchange(
                false,
                true,
                std::sync::atomic::Ordering::AcqRel,
                std::sync::atomic::Ordering::Acquire,
            )
            .map_err(|_| SubmitError::QueueFull)?;
        let validation = match &command {
            Command::SetAppearanceLayout(appearance, _) => appearance.validate(),
            Command::SetQueryPreferences(preferences, _) => preferences.validate(),
            Command::SetEditorPreferences(preferences, _) => preferences.validate(),
            Command::Save(documents, _) => choscordb_storage::validate_workspace(documents),
            Command::SaveTabs(snapshot, _) => choscordb_storage::validate_workspace_tabs(snapshot),
            Command::List(limit, offset, _) => {
                choscordb_storage::validate_history_page(*limit, *offset)
            }
            Command::SetPolicy(policy, _) => policy.validate(),
            Command::Record(entry, _) => entry.validate(),
            _ => Ok(()),
        };
        if let Err(error) = validation {
            self.recovery_pending
                .store(false, std::sync::atomic::Ordering::Release);
            return Err(invalid(error));
        }
        if let Err(error) = self
            .profiles
            .try_send(crate::profiles::Command::Recovery(command))
        {
            self.recovery_pending
                .store(false, std::sync::atomic::Ordering::Release);
            return Err(crate::map_send(error));
        }
        Ok(())
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
