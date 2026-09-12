//! Versioned defaults captured by future executions only.
use crate::{Result, Storage, StorageError};
use choscordb_driver_api::PageSize;
use serde::{Deserialize, Serialize};
pub const QUERY_PREFERENCES_VERSION: u32 = 1;
pub const MAX_QUERY_TIMEOUT_SECONDS: u32 = 86_400;
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct QueryPreferences {
    pub version: u32,
    pub page_size: PageSize,
    /// Zero maps to the driver's absent timeout.
    pub timeout_seconds: u32,
}
impl Default for QueryPreferences {
    fn default() -> Self {
        Self {
            version: QUERY_PREFERENCES_VERSION,
            page_size: PageSize::default(),
            timeout_seconds: 0,
        }
    }
}
impl QueryPreferences {
    pub fn validate(&self) -> Result<()> {
        if self.version != QUERY_PREFERENCES_VERSION
            || self.timeout_seconds > MAX_QUERY_TIMEOUT_SECONDS
        {
            return Err(StorageError::InvalidQueryPreferences);
        }
        Ok(())
    }
}
impl Storage {
    pub fn query_preferences(&self) -> Result<QueryPreferences> {
        let value: QueryPreferences = self.setting("query_preferences")?.unwrap_or_default();
        value.validate()?;
        Ok(value)
    }
    pub fn set_query_preferences(&mut self, value: &QueryPreferences) -> Result<()> {
        value.validate()?;
        self.db.execute("INSERT INTO settings(key,value) VALUES ('query_preferences',?1) ON CONFLICT(key) DO UPDATE SET value=excluded.value",[serde_json::to_string(value)?])?;
        Ok(())
    }
}
