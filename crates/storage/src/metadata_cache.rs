//! Saved navigator children, so a reconnect shows them before the server answers.
use crate::{Result, Storage, StorageError};
use choscordb_driver_api::{ObjectId, ObjectKind};
use rusqlite::{OptionalExtension, params};
use serde::{Deserialize, Serialize};

/// How long saved children stay valid.
pub const METADATA_CACHE_SECONDS: i64 = 7 * 24 * 60 * 60;

/// One saved child: its identity, its names and its kind only.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct CachedChild {
    pub id: ObjectId,
    pub name: String,
    pub qualified_name: String,
    pub kind: ObjectKind,
    pub has_children: bool,
}

/// The largest saved child list, so one entry cannot fill the local database.
const MAX_CACHED_BYTES: usize = 8 * 1024 * 1024;

fn key(parent: Option<&ObjectId>) -> &str {
    parent.map_or("", |parent| parent.0.as_str())
}

impl Storage {
    /// Saves the complete child list of `parent`. The root has no parent.
    pub fn cache_children(
        &mut self,
        profile_id: &str,
        parent: Option<&ObjectId>,
        children: &[CachedChild],
        now: i64,
    ) -> Result<()> {
        crate::validate_field(profile_id, 256)?;
        let data = serde_json::to_string(children)?;
        if data.len() > MAX_CACHED_BYTES {
            return Err(StorageError::ResourceLimit);
        }
        self.db
            .execute("DELETE FROM metadata_cache WHERE expires_at<?1", [now])?;
        self.db.execute(
            "INSERT INTO metadata_cache(profile_id,object_id,data,expires_at) VALUES(?1,?2,?3,?4)
             ON CONFLICT(profile_id,object_id) DO UPDATE SET data=excluded.data,expires_at=excluded.expires_at",
            params![
                profile_id,
                key(parent),
                data,
                now.saturating_add(METADATA_CACHE_SECONDS)
            ],
        )?;
        Ok(())
    }
    /// The saved children of `parent`, or `None` when none are saved or they expired.
    pub fn cached_children(
        &self,
        profile_id: &str,
        parent: Option<&ObjectId>,
        now: i64,
    ) -> Result<Option<Vec<CachedChild>>> {
        crate::validate_field(profile_id, 256)?;
        let data: Option<String> = self
            .db
            .query_row(
                "SELECT data FROM metadata_cache WHERE profile_id=?1 AND object_id=?2 AND expires_at>=?3",
                params![profile_id, key(parent), now],
                |row| row.get(0),
            )
            .optional()?;
        Ok(data.map(|data| serde_json::from_str(&data)).transpose()?)
    }
}
