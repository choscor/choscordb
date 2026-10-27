//! Versioned, credential-free editor preferences. Native clients validate the
//! syntax and conflicts of effective platform-specific key sequences.
use crate::{Result, Storage, StorageError};
use serde::{Deserialize, Serialize};
pub const EDITOR_PREFERENCES_VERSION: u32 = 2;
/// Version 1 commands that version 2 retired with the manual transaction mode.
const RETIRED_V1_COMMANDS: [&str; 2] = ["commit", "rollback"];
pub const DEFAULT_EDITOR_FONT_SIZE: u16 = 13;
pub const MIN_EDITOR_FONT_SIZE: u16 = 8;
pub const MAX_EDITOR_FONT_SIZE: u16 = 48;
pub const MAX_FONT_FAMILY_BYTES: usize = 256;
pub const MAX_SHORTCUT_SEQUENCE_BYTES: usize = 128;
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ShortcutCommand {
    NewQuery,
    ChoscorDBFile,
    SaveSqlFile,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    Find,
    Replace,
    FindNext,
    FindPrevious,
    RunStatement,
    RunAll,
    CancelQuery,
    GridAddRow,
    GridDuplicateRow,
    GridDeleteRows,
    GridRestoreRows,
    GridSetNull,
    GridEditCell,
    GridApplyEdits,
    GridDiscardEdits,
}
impl ShortcutCommand {
    pub const ALL: [Self; 23] = [
        Self::NewQuery,
        Self::ChoscorDBFile,
        Self::SaveSqlFile,
        Self::Undo,
        Self::Redo,
        Self::Cut,
        Self::Copy,
        Self::Paste,
        Self::Find,
        Self::Replace,
        Self::FindNext,
        Self::FindPrevious,
        Self::RunStatement,
        Self::RunAll,
        Self::CancelQuery,
        Self::GridAddRow,
        Self::GridDuplicateRow,
        Self::GridDeleteRows,
        Self::GridRestoreRows,
        Self::GridSetNull,
        Self::GridEditCell,
        Self::GridApplyEdits,
        Self::GridDiscardEdits,
    ];
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ShortcutOverride {
    pub command: ShortcutCommand,
    pub sequence: String,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct EditorPreferences {
    pub version: u32,
    pub font_family: Option<String>,
    pub font_size: u16,
    pub shortcuts: Vec<ShortcutOverride>,
}
impl Default for EditorPreferences {
    fn default() -> Self {
        Self {
            version: EDITOR_PREFERENCES_VERSION,
            font_family: None,
            font_size: DEFAULT_EDITOR_FONT_SIZE,
            shortcuts: Vec::new(),
        }
    }
}
impl EditorPreferences {
    pub fn validate(&self) -> Result<()> {
        if self.version != EDITOR_PREFERENCES_VERSION
            || !(MIN_EDITOR_FONT_SIZE..=MAX_EDITOR_FONT_SIZE).contains(&self.font_size)
        {
            return Err(StorageError::InvalidPreferences);
        }
        if self.shortcuts.len() > ShortcutCommand::ALL.len()
            || self
                .font_family
                .as_ref()
                .is_some_and(|s| s.len() > MAX_FONT_FAMILY_BYTES)
        {
            return Err(StorageError::ResourceLimit);
        }
        if self.font_family.as_ref().is_some_and(|s| s.contains('\0')) {
            return Err(StorageError::InvalidPreferences);
        }
        let mut commands = std::collections::HashSet::new();
        for item in &self.shortcuts {
            if item.sequence.len() > MAX_SHORTCUT_SEQUENCE_BYTES {
                return Err(StorageError::ResourceLimit);
            }
            if item.sequence.contains('\0') || !commands.insert(item.command) {
                return Err(StorageError::InvalidPreferences);
            }
        }
        Ok(())
    }
}
impl Storage {
    pub fn editor_preferences(&self) -> Result<EditorPreferences> {
        let Some(mut stored) = self.setting::<serde_json::Value>("editor_preferences")? else {
            return Ok(EditorPreferences::default());
        };
        let migrate = stored.get("version") == Some(&serde_json::Value::from(1));
        if migrate {
            migrate_version_one(&mut stored)?;
        }
        let preferences: EditorPreferences = serde_json::from_value(stored)?;
        preferences.validate()?;
        if migrate {
            self.write_editor_preferences(&preferences)?;
        }
        Ok(preferences)
    }
    pub fn set_editor_preferences(&mut self, preferences: &EditorPreferences) -> Result<()> {
        preferences.validate()?;
        self.write_editor_preferences(preferences)
    }
    fn write_editor_preferences(&self, preferences: &EditorPreferences) -> Result<()> {
        let encoded = serde_json::to_string(preferences)?;
        self.db.execute("INSERT INTO settings(key,value) VALUES ('editor_preferences',?1) ON CONFLICT(key) DO UPDATE SET value=excluded.value",[encoded])?;
        Ok(())
    }
}

/// Drops the retired version 1 commands. Any other unknown command still fails to parse.
fn migrate_version_one(stored: &mut serde_json::Value) -> Result<()> {
    let shortcuts = stored
        .get_mut("shortcuts")
        .and_then(serde_json::Value::as_array_mut)
        .ok_or(StorageError::InvalidPreferences)?;
    shortcuts.retain(|item| {
        !item
            .get("command")
            .and_then(serde_json::Value::as_str)
            .is_some_and(|command| RETIRED_V1_COMMANDS.contains(&command))
    });
    stored["version"] = EDITOR_PREFERENCES_VERSION.into();
    Ok(())
}
