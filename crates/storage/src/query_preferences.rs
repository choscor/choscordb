//! Versioned defaults for future queries and tunneled connections.
use crate::{Result, Storage, StorageError};
use choscordb_driver_api::PageSize;
use serde::{Deserialize, Serialize};
pub const QUERY_PREFERENCES_VERSION: u32 = 1;
pub const MAX_QUERY_TIMEOUT_SECONDS: u32 = 86_400;
pub const DEFAULT_CONNECTION_TIMEOUT_SECONDS: u32 = 20;
pub const MAX_CONNECTION_TIMEOUT_SECONDS: u32 = 300;
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct QueryPreferences {
    pub version: u32,
    pub page_size: PageSize,
    /// Zero maps to the driver's absent timeout.
    pub timeout_seconds: u32,
    /// Global limit for new SSH tunnels and database logins through them.
    #[serde(default = "default_connection_timeout_seconds")]
    pub connection_timeout_seconds: u32,
    /// Whether PostgreSQL-owned schemas appear in browsing and completion.
    #[serde(default)]
    pub show_system_schemas: bool,
}
fn default_connection_timeout_seconds() -> u32 {
    DEFAULT_CONNECTION_TIMEOUT_SECONDS
}
impl Default for QueryPreferences {
    fn default() -> Self {
        Self {
            version: QUERY_PREFERENCES_VERSION,
            page_size: PageSize::default(),
            timeout_seconds: 0,
            connection_timeout_seconds: DEFAULT_CONNECTION_TIMEOUT_SECONDS,
            show_system_schemas: false,
        }
    }
}
impl QueryPreferences {
    pub fn validate(&self) -> Result<()> {
        if self.version != QUERY_PREFERENCES_VERSION
            || self.timeout_seconds > MAX_QUERY_TIMEOUT_SECONDS
            || !(1..=MAX_CONNECTION_TIMEOUT_SECONDS).contains(&self.connection_timeout_seconds)
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
