use crate::{MAX_SETTING_BYTES, Result, Storage, StorageError, encode_bounded};
use rusqlite::params;
use serde::{Deserialize, Serialize};

pub const MAX_HIDDEN_COLUMN_CONTEXTS: usize = 128;
pub const MAX_HIDDEN_COLUMN_CONTEXT_BYTES: usize = 1024;
pub const MAX_HIDDEN_COLUMN_NAME_BYTES: usize = 256;
pub const MAX_HIDDEN_COLUMNS: usize = 256;
const MAX_HIDDEN_ENTRY_BYTES: usize = 16 * 1024;
const SETTING_KEY: &str = "result_column_layouts";

#[derive(Clone, Copy, Debug, PartialEq, Eq, thiserror::Error)]
pub enum ColumnLayoutError {
    #[error("The result must show at least one column")]
    LastVisibleColumn,
    #[error("The column is not in the result")]
    UnknownColumn,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct HiddenColumns {
    context: String,
    hidden: Vec<String>,
}

pub fn result_columns_context(profile_id: &str, object_id: &str) -> String {
    if profile_id.is_empty() || object_id.is_empty() {
        return String::new();
    }
    serde_json::Value::from(vec![profile_id, object_id]).to_string()
}

pub fn toggle_hidden_column(
    hidden: &[String],
    name: &str,
    hide: bool,
    columns: &[String],
) -> std::result::Result<Vec<String>, ColumnLayoutError> {
    if !columns.iter().any(|column| column == name) {
        return Err(ColumnLayoutError::UnknownColumn);
    }
    let mut next: Vec<String> = hidden
        .iter()
        .filter(|column| column.as_str() != name)
        .cloned()
        .collect();
    if hide {
        next.push(name.to_owned());
        if columns.iter().all(|column| next.contains(column)) {
            return Err(ColumnLayoutError::LastVisibleColumn);
        }
    }
    Ok(next)
}

pub fn validate_hidden_result_columns(context: &str, hidden: &[String]) -> Result<()> {
    let context_valid = !context.is_empty()
        && context.len() <= MAX_HIDDEN_COLUMN_CONTEXT_BYTES
        && !context.contains('\0');
    let names_valid = hidden.len() <= MAX_HIDDEN_COLUMNS
        && hidden.iter().enumerate().all(|(index, name)| {
            !name.is_empty()
                && name.len() <= MAX_HIDDEN_COLUMN_NAME_BYTES
                && !name.contains('\0')
                && !hidden[..index].contains(name)
        })
        && hidden.iter().map(String::len).sum::<usize>() <= MAX_HIDDEN_ENTRY_BYTES;
    if context_valid && names_valid {
        Ok(())
    } else {
        Err(StorageError::InvalidColumnLayout)
    }
}

fn validate_entry(entry: &HiddenColumns) -> Result<()> {
    validate_hidden_result_columns(&entry.context, &entry.hidden)
}

impl Storage {
    fn hidden_column_entries(&self) -> Result<Vec<HiddenColumns>> {
        let entries: Vec<HiddenColumns> = self.setting(SETTING_KEY)?.unwrap_or_default();
        if entries.len() > MAX_HIDDEN_COLUMN_CONTEXTS {
            return Err(StorageError::InvalidColumnLayout);
        }
        for entry in &entries {
            validate_entry(entry)?;
        }
        Ok(entries)
    }

    pub fn hidden_result_columns(&self, context: &str) -> Result<Vec<String>> {
        Ok(self
            .hidden_column_entries()?
            .into_iter()
            .find(|entry| entry.context == context)
            .map(|entry| entry.hidden)
            .unwrap_or_default())
    }

    pub fn set_hidden_result_columns(&mut self, context: &str, hidden: &[String]) -> Result<()> {
        let entry = HiddenColumns {
            context: context.to_owned(),
            hidden: hidden.to_vec(),
        };
        validate_entry(&entry)?;
        let mut entries = self.hidden_column_entries()?;
        entries.retain(|stored| stored.context != context);
        if !entry.hidden.is_empty() {
            entries.insert(0, entry);
        }
        entries.truncate(MAX_HIDDEN_COLUMN_CONTEXTS);
        let encoded = loop {
            match encode_bounded(&entries, MAX_SETTING_BYTES) {
                Err(StorageError::ResourceLimit) if entries.len() > 1 => {
                    entries.pop();
                }
                result => break result?,
            }
        };
        self.db.execute(
            "INSERT INTO settings(key,value) VALUES (?1,?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value",
            params![SETTING_KEY, encoded],
        )?;
        Ok(())
    }
}
