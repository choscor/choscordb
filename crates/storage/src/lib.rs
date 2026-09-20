//! Synchronous local metadata persistence. Call only from a dedicated blocking worker.
//! Recovery returns inert editor data: this crate has no driver or execution dependency.

mod appearance;
mod preferences;
mod query_preferences;
pub use appearance::{
    APPEARANCE_LAYOUT_VERSION, Accent, AccentPreset, AppearanceLayout, Density,
    MAX_APPEARANCE_LAYOUT_BYTES, MAX_SCREEN_NAME_BYTES, MAX_WINDOW_DIMENSION, MIN_WINDOW_HEIGHT,
    MIN_WINDOW_WIDTH, ThemeMode, WindowGeometry, WorkspaceLayout,
};
use choscordb_driver_api::{ConnectionOptions, Secret};
pub use choscordb_driver_api::{SshTunnel, TlsMode};
pub use preferences::{
    DEFAULT_EDITOR_FONT_SIZE, EDITOR_PREFERENCES_VERSION, EditorPreferences, MAX_EDITOR_FONT_SIZE,
    MAX_FONT_FAMILY_BYTES, MAX_SHORTCUT_SEQUENCE_BYTES, MIN_EDITOR_FONT_SIZE, ShortcutCommand,
    ShortcutOverride,
};
pub use query_preferences::{
    MAX_QUERY_TIMEOUT_SECONDS, QUERY_PREFERENCES_VERSION, QueryPreferences,
};
use rusqlite::{Connection, params};
use serde::{Deserialize, Serialize, de::DeserializeOwned};
use std::path::Path;

#[derive(Debug, thiserror::Error)]
pub enum StorageError {
    #[error("invalid editor preferences")]
    InvalidPreferences,
    #[error("invalid query preferences")]
    InvalidQueryPreferences,
    #[error("invalid appearance or layout preferences")]
    InvalidAppearance,
    #[error("stored appearance or layout preferences are corrupt")]
    CorruptAppearance,
    #[error("stored appearance or layout version {0} is unsupported")]
    UnsupportedAppearanceVersion(u64),
    #[error("local database operation failed: {0}")]
    Database(#[from] rusqlite::Error),
    #[error("stored data encoding is invalid: {0}")]
    Encoding(#[from] serde_json::Error),
    #[error("profile does not exist")]
    ProfileMissing,
    #[error("invalid connection profile")]
    InvalidProfile,
    #[error("local metadata resource limit exceeded")]
    ResourceLimit,
    #[error("this setting must be changed through its dedicated API")]
    ReservedSetting,
    #[error("invalid editor recovery or history data")]
    InvalidDocument,
    #[error("invalid history retention policy")]
    InvalidRetention,
    #[error("local database schema is newer than this application")]
    NewerSchema,
}
pub type Result<T> = std::result::Result<T, StorageError>;

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ConnectionProfile {
    pub id: String,
    pub name: String,
    pub group_id: Option<String>,
    pub configuration: ProfileConfiguration,
    /// Opaque identifier for an OS credential item, never the credential itself.
    pub credential_ref: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "driver", rename_all = "snake_case", deny_unknown_fields)]
pub enum ProfileConfiguration {
    Sqlite {
        path: String,
        read_only: bool,
    },
    Postgres {
        host: String,
        port: u16,
        database: String,
        user: String,
        tls: PostgresTls,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        ssh: Option<SshTunnel>,
    },
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PostgresTls {
    pub mode: TlsMode,
    pub root_certificate_path: Option<String>,
}

#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct EditorDocument {
    pub id: String,
    pub title: String,
    pub sql: String,
    pub profile_id: Option<String>,
    pub file_path: Option<String>,
    pub cursor_offset: u64,
    pub selection_anchor: u64,
    pub modified: bool,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ObjectTab {
    pub profile_id: String,
    pub object_type: String,
    pub object_id: String,
    pub label: String,
    pub pane: u32,
}
#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", content = "data", rename_all = "snake_case")]
pub enum WorkspaceTab {
    Sql(EditorDocument),
    Object(ObjectTab),
}
#[derive(Clone, PartialEq, Eq)]
pub struct WorkspaceSnapshot {
    pub tabs: Vec<WorkspaceTab>,
    pub active_index: usize,
}
impl std::fmt::Debug for WorkspaceTab {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("WorkspaceTab([REDACTED])")
    }
}
impl std::fmt::Debug for WorkspaceSnapshot {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("WorkspaceSnapshot([REDACTED])")
    }
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HistoryStatus {
    Completed,
    Failed,
    Cancelled,
    Disconnected,
}
#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct HistoryEntry {
    pub id: String,
    pub profile_id: Option<String>,
    pub sql: String,
    /// Unix timestamp in seconds.
    pub timestamp: i64,
    pub duration_ms: u64,
    pub status: HistoryStatus,
    pub row_count: Option<u64>,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct HistoryPolicy {
    pub enabled: bool,
    pub max_age_days: u32,
    pub max_records: u32,
}
impl Default for HistoryPolicy {
    fn default() -> Self {
        Self {
            enabled: true,
            max_age_days: 90,
            max_records: 10_000,
        }
    }
}

pub struct Storage {
    db: Connection,
}
impl Storage {
    pub fn open(path: impl AsRef<Path>) -> Result<Self> {
        Self::initialize(Connection::open(path)?)
    }
    pub fn in_memory() -> Result<Self> {
        Self::initialize(Connection::open_in_memory()?)
    }
    fn initialize(mut db: Connection) -> Result<Self> {
        db.busy_timeout(std::time::Duration::from_secs(5))?;
        db.pragma_update(None, "foreign_keys", true)?;
        let tx = db.transaction()?;
        tx.execute_batch(
            "CREATE TABLE IF NOT EXISTS schema_migrations(version INTEGER PRIMARY KEY);",
        )?;
        let version: i64 = tx.query_row(
            "SELECT coalesce(max(version),0) FROM schema_migrations",
            [],
            |r| r.get(0),
        )?;
        if version > 3 {
            return Err(StorageError::NewerSchema);
        }
        if version == 0 {
            tx.execute_batch(include_str!("schema.sql"))?;
            tx.execute("INSERT INTO schema_migrations(version) VALUES (1)", [])?;
        }
        if version < 2 {
            tx.execute_batch(
                "CREATE TABLE pending_credential_cleanup(reference TEXT PRIMARY KEY NOT NULL);",
            )?;
            tx.execute("INSERT INTO schema_migrations(version) VALUES (2)", [])?;
        }
        if version < 3 {
            tx.execute_batch(
                "CREATE TABLE IF NOT EXISTS appearance_layout(\
                    singleton INTEGER PRIMARY KEY NOT NULL CHECK(singleton=1),\
                    value TEXT NOT NULL\
                );",
            )?;
            tx.execute("INSERT INTO schema_migrations(version) VALUES (3)", [])?;
        }
        tx.commit()?;
        let mut store = Self { db };
        // Corrupt history preferences must not disable profiles or inert recovery.
        // History APIs retain the error until the user replaces the policy.
        match store.history_policy() {
            Ok(policy) => {
                let tx = store.db.transaction()?;
                prune(&tx, &policy, unix_now())?;
                tx.commit()?;
            }
            Err(
                StorageError::Encoding(_)
                | StorageError::InvalidDocument
                | StorageError::InvalidRetention
                | StorageError::ResourceLimit,
            ) => {}
            Err(error) => return Err(error),
        }
        Ok(store)
    }
    pub fn save_profile(&mut self, profile: &ConnectionProfile) -> Result<()> {
        let data = encode_profile(profile)?;
        let tx = self
            .db
            .transaction_with_behavior(rusqlite::TransactionBehavior::Immediate)?;
        let previous = credential_reference(&tx, &profile.id)?;
        let exists: bool = tx.query_row(
            "SELECT EXISTS(SELECT 1 FROM connection_profiles WHERE id=?1)",
            [&profile.id],
            |r| r.get(0),
        )?;
        if !exists {
            check_profile_capacity(&tx)?;
        }
        tx.execute("INSERT INTO connection_profiles(id, data) VALUES (?1, ?2) ON CONFLICT(id) DO UPDATE SET data=excluded.data", params![profile.id, data])?;
        if let Some(reference) = previous.filter(|r| Some(r) != profile.credential_ref.as_ref()) {
            enqueue_cleanup(&tx, &reference)?;
        }
        tx.commit()?;
        Ok(())
    }
    pub fn profile(&self, id: &str) -> Result<Option<ConnectionProfile>> {
        validate_field(id, 256)?;
        let mut stmt = self
            .db
            .prepare("SELECT id, data FROM connection_profiles WHERE id=?1")?;
        let mut rows = stmt.query([id])?;
        rows.next()?.map(decode_profile).transpose()
    }
    /// Returns all profiles or an error, never a silently truncated list.
    pub fn profiles(&self) -> Result<Vec<ConnectionProfile>> {
        let mut stmt = self
            .db
            .prepare("SELECT id, data FROM connection_profiles ORDER BY id LIMIT 1001")?;
        let mut rows = stmt.query([])?;
        let mut profiles = Vec::new();
        while let Some(row) = rows.next()? {
            if profiles.len() == MAX_PROFILES {
                return Err(StorageError::ResourceLimit);
            }
            profiles.push(decode_profile(row)?);
        }
        Ok(profiles)
    }
    /// Copies configuration only. The copy must obtain its own OS credential item;
    /// references are never shared because replacement/deletion owns that item.
    pub fn duplicate_profile(
        &mut self,
        source: &str,
        id: &str,
        name: &str,
    ) -> Result<ConnectionProfile> {
        let mut profile = self.profile(source)?.ok_or(StorageError::ProfileMissing)?;
        profile.id = id.into();
        profile.name = name.into();
        profile.credential_ref = None;
        let data = encode_profile(&profile)?;
        let tx = self
            .db
            .transaction_with_behavior(rusqlite::TransactionBehavior::Immediate)?;
        check_profile_capacity(&tx)?;
        tx.execute(
            "INSERT INTO connection_profiles(id,data) VALUES (?1,?2)",
            params![id, data],
        )?;
        tx.commit()?;
        Ok(profile)
    }
    pub fn delete_profile(&mut self, id: &str) -> Result<bool> {
        validate_field(id, 256)?;
        let tx = self
            .db
            .transaction_with_behavior(rusqlite::TransactionBehavior::Immediate)?;
        let previous = credential_reference(&tx, id)?;
        let removed = tx.execute("DELETE FROM connection_profiles WHERE id=?1", [id])? != 0;
        if let Some(reference) = previous {
            enqueue_cleanup(&tx, &reference)?;
        }
        tx.commit()?;
        Ok(removed)
    }
    pub fn pending_credential_cleanup(&self) -> Result<Vec<String>> {
        let mut statement = self.db.prepare(
            "SELECT reference FROM pending_credential_cleanup ORDER BY reference LIMIT 4097",
        )?;
        let mut rows = statement.query([])?;
        let mut refs = Vec::new();
        while let Some(row) = rows.next()? {
            if refs.len() == 4096 {
                return Err(StorageError::ResourceLimit);
            }
            let reference = row
                .get_ref(0)?
                .as_str()
                .map_err(|_| StorageError::InvalidProfile)?;
            validate_field(reference, 256)?;
            refs.push(reference.to_owned());
        }
        Ok(refs)
    }
    pub fn acknowledge_credential_cleanup(&mut self, reference: &str) -> Result<()> {
        self.db.execute(
            "DELETE FROM pending_credential_cleanup WHERE reference=?1",
            [reference],
        )?;
        Ok(())
    }
    pub fn queue_credential_cleanup(&mut self, reference: &str) -> Result<()> {
        validate_field(reference, 256)?;
        enqueue_cleanup(&self.db, reference)
    }
    pub fn credential_is_referenced(&self, reference: &str) -> Result<bool> {
        Ok(self
            .profiles()?
            .iter()
            .any(|p| p.credential_ref.as_deref() == Some(reference)))
    }
    /// Application-owned preferences only. Never pass credentials or connection strings.
    pub fn set_setting<T: Serialize>(&mut self, key: &str, value: &T) -> Result<()> {
        if key == "history_policy"
            || key == "editor_preferences"
            || key == "query_preferences"
            || key == "appearance_layout"
        {
            return Err(StorageError::ReservedSetting);
        }
        self.db.execute("INSERT INTO settings(key,value) VALUES (?1,?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value", params![key, encode_bounded(value, MAX_SETTING_BYTES)?])?;
        Ok(())
    }
    pub fn setting<T: DeserializeOwned>(&self, key: &str) -> Result<Option<T>> {
        let mut stmt = self.db.prepare("SELECT value FROM settings WHERE key=?1")?;
        let mut rows = stmt.query([key])?;
        let Some(row) = rows.next()? else {
            return Ok(None);
        };
        let data = bounded_data(row, 0, MAX_SETTING_BYTES, &mut 0)?;
        Ok(Some(serde_json::from_str(data)?))
    }

    pub fn save_workspace(&mut self, documents: &[EditorDocument]) -> Result<()> {
        validate_workspace(documents)?;
        let tx = self.db.transaction()?;
        tx.execute("DELETE FROM editor_documents", [])?;
        tx.execute("DELETE FROM settings WHERE key='workspace_active_tab'", [])?;
        for (position, document) in documents.iter().enumerate() {
            tx.execute(
                "INSERT INTO editor_documents(id,position,data) VALUES (?1,?2,?3)",
                params![
                    document.id,
                    position as i64,
                    serde_json::to_string(document)?
                ],
            )?;
        }
        tx.commit()?;
        Ok(())
    }
    pub fn restore_workspace(&self) -> Result<Vec<EditorDocument>> {
        let mut stmt = self
            .db
            .prepare("SELECT id,position,data FROM editor_documents ORDER BY position LIMIT 129")?;
        let mut rows = stmt.query([])?;
        let mut documents = Vec::new();
        let mut total = 0;
        while let Some(row) = rows.next()? {
            if documents.len() == MAX_WORKSPACE_DOCUMENTS {
                return Err(StorageError::ResourceLimit);
            }
            let data = bounded_data(row, 2, MAX_RECORD_BYTES, &mut total)?;
            let document: EditorDocument =
                serde_json::from_str(data).map_err(|_| StorageError::InvalidDocument)?;
            document.validate()?;
            if row.get_ref(0)?.as_str().ok() != Some(document.id.as_str())
                || row.get::<_, i64>(1)? != documents.len() as i64
            {
                return Err(StorageError::InvalidDocument);
            }
            documents.push(document);
        }
        Ok(documents)
    }
    pub fn save_workspace_tabs(&mut self, snapshot: &WorkspaceSnapshot) -> Result<()> {
        validate_workspace_tabs(snapshot)?;
        let tx = self.db.transaction()?;
        tx.execute("DELETE FROM editor_documents", [])?;
        for (position, tab) in snapshot.tabs.iter().enumerate() {
            let id = tab.storage_id();
            tx.execute(
                "INSERT INTO editor_documents(id,position,data) VALUES (?1,?2,?3)",
                params![id, position as i64, serde_json::to_string(tab)?],
            )?;
        }
        tx.execute("INSERT INTO settings(key,value) VALUES ('workspace_active_tab',?1) ON CONFLICT(key) DO UPDATE SET value=excluded.value",
            [snapshot.active_index.to_string()])?;
        tx.commit()?;
        Ok(())
    }
    pub fn restore_workspace_tabs(&self) -> Result<WorkspaceSnapshot> {
        let mut stmt = self
            .db
            .prepare("SELECT id,position,data FROM editor_documents ORDER BY position LIMIT 129")?;
        let mut rows = stmt.query([])?;
        let mut tabs = Vec::new();
        let mut total = 0;
        while let Some(row) = rows.next()? {
            if tabs.len() == MAX_WORKSPACE_DOCUMENTS {
                return Err(StorageError::ResourceLimit);
            }
            let data = bounded_data(row, 2, MAX_RECORD_BYTES, &mut total)?;
            let (tab, expected_id, legacy_id): (WorkspaceTab, String, Option<String>) =
                match serde_json::from_str::<WorkspaceTab>(data) {
                    Ok(tab) => {
                        let id = tab.storage_id();
                        let legacy = match &tab {
                            WorkspaceTab::Sql(d) => d.id.clone(),
                            WorkspaceTab::Object(o) => {
                                format!("object:{}:{}:{}", o.profile_id, o.object_type, o.object_id)
                            }
                        };
                        (tab, id, Some(legacy))
                    }
                    Err(_) => {
                        let document: EditorDocument = serde_json::from_str(data)
                            .map_err(|_| StorageError::InvalidDocument)?;
                        let id = document.id.clone();
                        (WorkspaceTab::Sql(document), id, None)
                    }
                };
            tab.validate()?;
            let row_id = row.get_ref(0)?.as_str().ok();
            if (row_id != Some(expected_id.as_str()) && row_id != legacy_id.as_deref())
                || row.get::<_, i64>(1)? != tabs.len() as i64
            {
                return Err(StorageError::InvalidDocument);
            }
            tabs.push(tab);
        }
        let active_index = self.setting::<usize>("workspace_active_tab")?.unwrap_or(0);
        let snapshot = WorkspaceSnapshot { tabs, active_index };
        validate_workspace_tabs(&snapshot)?;
        Ok(snapshot)
    }
    pub fn history_policy(&self) -> Result<HistoryPolicy> {
        let policy: HistoryPolicy = self.setting("history_policy")?.unwrap_or_default();
        policy.validate()?;
        Ok(policy)
    }
    pub fn set_history_policy(&mut self, policy: HistoryPolicy) -> Result<()> {
        policy.validate()?;
        let tx = self.db.transaction()?;
        tx.execute("INSERT INTO settings(key,value) VALUES ('history_policy',?1) ON CONFLICT(key) DO UPDATE SET value=excluded.value", [serde_json::to_string(&policy)?])?;
        prune(&tx, &policy, unix_now())?;
        tx.commit()?;
        Ok(())
    }
    pub fn record_history(&mut self, entry: &HistoryEntry, now: i64) -> Result<bool> {
        let policy = self.history_policy()?;
        if !policy.enabled {
            return Ok(false);
        }
        entry.validate()?;
        let data = encode_bounded(entry, MAX_COLLECTION_BYTES)?;
        let tx = self.db.transaction()?;
        tx.execute(
            "INSERT INTO query_history(id,timestamp,data) VALUES (?1,?2,?3)",
            params![entry.id, entry.timestamp, data],
        )?;
        prune(&tx, &policy, now)?;
        tx.commit()?;
        Ok(true)
    }
    /// Invoke at application startup as well as after inserts to age out idle history.
    pub fn prune_history(&mut self, now: i64) -> Result<()> {
        let policy = self.history_policy()?;
        let tx = self.db.transaction()?;
        prune(&tx, &policy, now)?;
        tx.commit()?;
        Ok(())
    }
    /// Return at most `limit` records, stopping before the serialized page byte cap.
    /// A nonempty page can be shorter than `limit` without reaching EOF. Advance
    /// the next offset by the returned length; no record is skipped or truncated.
    pub fn history(&self, limit: u32, offset: u32) -> Result<Vec<HistoryEntry>> {
        self.history_policy()?;
        validate_history_page(limit, offset)?;
        let mut stmt = self.db.prepare("SELECT id,timestamp,data FROM query_history ORDER BY timestamp DESC,rowid DESC LIMIT ?1 OFFSET ?2")?;
        let mut rows = stmt.query(params![limit, offset])?;
        let mut entries = Vec::new();
        let mut total = 0;
        while let Some(row) = rows.next()? {
            let data = row
                .get_ref(2)?
                .as_str()
                .map_err(|_| StorageError::InvalidDocument)?;
            if data.len() > MAX_COLLECTION_BYTES - total {
                if entries.is_empty() {
                    return Err(StorageError::ResourceLimit);
                }
                break;
            }
            total += data.len();
            let entry: HistoryEntry =
                serde_json::from_str(data).map_err(|_| StorageError::InvalidDocument)?;
            entry.validate()?;
            if row.get_ref(0)?.as_str().ok() != Some(entry.id.as_str())
                || row.get::<_, i64>(1)? != entry.timestamp
            {
                return Err(StorageError::InvalidDocument);
            }
            entries.push(entry);
        }
        Ok(entries)
    }
    pub fn clear_history(&mut self) -> Result<()> {
        self.db.execute("DELETE FROM query_history", [])?;
        Ok(())
    }
}

fn prune(tx: &rusqlite::Transaction<'_>, policy: &HistoryPolicy, now: i64) -> Result<()> {
    let cutoff = now.saturating_sub(i64::from(policy.max_age_days) * 86400);
    tx.execute("DELETE FROM query_history WHERE timestamp < ?1", [cutoff])?;
    tx.execute("DELETE FROM query_history WHERE rowid IN (SELECT rowid FROM query_history ORDER BY timestamp DESC,rowid DESC LIMIT -1 OFFSET ?1)", [policy.max_records])?;
    Ok(())
}

fn unix_now() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|duration| i64::try_from(duration.as_secs()).unwrap_or(i64::MAX))
        .unwrap_or(0)
}

impl std::fmt::Debug for EditorDocument {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("EditorDocument([REDACTED])")
    }
}
impl std::fmt::Debug for HistoryEntry {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("HistoryEntry([REDACTED])")
    }
}

impl ProfileConfiguration {
    /// Convert all stored options without weakening TLS. Resolve the credential
    /// separately through the OS adapter; credentials never enter this DTO.
    pub fn connection_options(&self, password: Option<Secret>) -> ConnectionOptions {
        match self {
            Self::Sqlite { path, read_only } => ConnectionOptions::Sqlite {
                path: path.into(),
                read_only: *read_only,
            },
            Self::Postgres {
                host,
                port,
                database,
                user,
                tls,
                ssh,
            } => ConnectionOptions::Postgres {
                host: host.clone(),
                port: *port,
                database: database.clone(),
                user: user.clone(),
                password,
                tls: tls.mode.clone(),
                root_certificate: tls.root_certificate_path.as_ref().map(Into::into),
                ssh: ssh.clone(),
            },
        }
    }
}

/// Hard bounds apply equally to newly submitted and persisted profiles.
pub const MAX_PROFILES: usize = 1000;
pub const MAX_PROFILE_BYTES: usize = 64 * 1024;

fn validate_field(value: &str, limit: usize) -> Result<()> {
    if value.trim().is_empty() || value.contains('\0') {
        return Err(StorageError::InvalidProfile);
    }
    if value.len() > limit {
        return Err(StorageError::ResourceLimit);
    }
    Ok(())
}
impl ConnectionProfile {
    pub fn validate(&self) -> Result<()> {
        validate_field(&self.id, 256)?;
        validate_field(&self.name, 1024)?;
        for field in [&self.group_id, &self.credential_ref].into_iter().flatten() {
            validate_field(field, 16 * 1024)?;
        }
        match &self.configuration {
            ProfileConfiguration::Sqlite { path, .. } => validate_field(path, 16 * 1024)?,
            ProfileConfiguration::Postgres {
                host,
                port,
                database,
                user,
                tls,
                ssh,
            } => {
                if let Some(ssh) = ssh {
                    ssh.validate().map_err(|_| StorageError::InvalidProfile)?;
                }
                if *port == 0 {
                    return Err(StorageError::InvalidProfile);
                }
                for field in [host, database, user] {
                    validate_field(field, 16 * 1024)?;
                }
                if let Some(path) = &tls.root_certificate_path {
                    validate_field(path, 16 * 1024)?;
                }
            }
        }
        Ok(())
    }
}
fn check_profile_capacity(db: &Connection) -> Result<()> {
    let count: usize = db.query_row(
        "SELECT count(*) FROM (SELECT 1 FROM connection_profiles LIMIT 1000)",
        [],
        |r| r.get(0),
    )?;
    if count >= MAX_PROFILES {
        return Err(StorageError::ResourceLimit);
    }
    Ok(())
}
fn encode_profile(profile: &ConnectionProfile) -> Result<String> {
    profile.validate()?;
    // Count escaped JSON before allocating its output buffer.
    struct Counter(usize);
    impl std::io::Write for Counter {
        fn write(&mut self, bytes: &[u8]) -> std::io::Result<usize> {
            self.0 = self.0.saturating_add(bytes.len());
            if self.0 > MAX_PROFILE_BYTES {
                return Err(std::io::Error::other("profile size limit"));
            }
            Ok(bytes.len())
        }
        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }
    let mut count = Counter(0);
    serde_json::to_writer(&mut count, profile).map_err(|_| StorageError::ResourceLimit)?;
    let mut bytes = Vec::with_capacity(count.0);
    serde_json::to_writer(&mut bytes, profile)?;
    String::from_utf8(bytes).map_err(|_| StorageError::InvalidProfile)
}
fn decode_profile(row: &rusqlite::Row<'_>) -> Result<ConnectionProfile> {
    // Borrow SQLite's buffer and check bytes before deserialization allocates strings.
    let data = row
        .get_ref(1)?
        .as_str()
        .map_err(|_| StorageError::InvalidProfile)?;
    if data.len() > MAX_PROFILE_BYTES {
        return Err(StorageError::ResourceLimit);
    }
    let profile: ConnectionProfile =
        serde_json::from_str(data).map_err(|_| StorageError::InvalidProfile)?;
    profile.validate()?;
    let id = row
        .get_ref(0)?
        .as_str()
        .map_err(|_| StorageError::InvalidProfile)?;
    if id != profile.id {
        return Err(StorageError::InvalidProfile);
    }
    Ok(profile)
}

fn enqueue_cleanup(db: &Connection, reference: &str) -> Result<()> {
    let (count, exists): (i64, bool) = db.query_row("SELECT (SELECT count(*) FROM pending_credential_cleanup), EXISTS(SELECT 1 FROM pending_credential_cleanup WHERE reference=?1)", [reference], |r| Ok((r.get(0)?, r.get(1)?)))?;
    if exists {
        return Ok(());
    }
    if count >= 4096 {
        return Err(StorageError::ResourceLimit);
    }
    db.execute(
        "INSERT OR IGNORE INTO pending_credential_cleanup(reference) VALUES (?1)",
        [reference],
    )?;
    Ok(())
}

fn credential_reference(db: &Connection, id: &str) -> Result<Option<String>> {
    let mut statement = db.prepare("SELECT id,data FROM connection_profiles WHERE id=?1")?;
    let mut rows = statement.query([id])?;
    rows.next()?
        .map(decode_profile)
        .transpose()
        .map(|p| p.and_then(|p| p.credential_ref))
}

/// SQL positions use UTF-8 byte offsets, matching Scintilla.
pub const MAX_SQL_BYTES: usize = 16 * 1024 * 1024;
pub const MAX_WORKSPACE_DOCUMENTS: usize = 128;
pub const MAX_HISTORY_PAGE: u32 = 1000;
pub const MAX_COLLECTION_BYTES: usize = 64 * 1024 * 1024;
pub const MAX_RECORD_BYTES: usize = MAX_SQL_BYTES * 6 + 64 * 1024;
pub const MAX_SETTING_BYTES: usize = 64 * 1024;
fn document_field(value: &str, max: usize) -> Result<()> {
    validate_field(value, max).map_err(|e| match e {
        StorageError::InvalidProfile => StorageError::InvalidDocument,
        other => other,
    })
}
impl EditorDocument {
    pub fn validate(&self) -> Result<()> {
        document_field(&self.id, 256)?;
        document_field(&self.title, 1024)?;
        if let Some(id) = &self.profile_id {
            document_field(id, 256)?;
        }
        if let Some(path) = &self.file_path {
            document_field(path, 16 * 1024)?;
        }
        if self.sql.len() > MAX_SQL_BYTES {
            return Err(StorageError::ResourceLimit);
        }
        for offset in [self.cursor_offset, self.selection_anchor] {
            let offset = usize::try_from(offset).map_err(|_| StorageError::InvalidDocument)?;
            if !self.sql.is_char_boundary(offset) {
                return Err(StorageError::InvalidDocument);
            }
        }
        Ok(())
    }
}
impl HistoryEntry {
    pub fn validate(&self) -> Result<()> {
        document_field(&self.id, 256)?;
        if let Some(id) = &self.profile_id {
            document_field(id, 256)?;
        }
        if self.sql.len() > MAX_SQL_BYTES {
            return Err(StorageError::ResourceLimit);
        }
        encoded_size(self, MAX_COLLECTION_BYTES)?;
        Ok(())
    }
}
/// Validate before queueing a snapshot. Rejection never partially replaces saved tabs.
pub fn validate_workspace(documents: &[EditorDocument]) -> Result<()> {
    if documents.len() > MAX_WORKSPACE_DOCUMENTS {
        return Err(StorageError::ResourceLimit);
    }
    let mut ids = std::collections::HashSet::new();
    let mut total = 0usize;
    for document in documents {
        document.validate()?;
        if !ids.insert(&document.id) {
            return Err(StorageError::InvalidDocument);
        }
        total = total.saturating_add(encoded_size(document, MAX_RECORD_BYTES)?);
        if total > MAX_COLLECTION_BYTES {
            return Err(StorageError::ResourceLimit);
        }
    }
    Ok(())
}
impl WorkspaceTab {
    fn storage_id(&self) -> String {
        match self {
            Self::Sql(d) => format!("sql:{}", d.id),
            Self::Object(o) => format!(
                "object:{}:{}:{}:{}:{}:{}",
                o.profile_id.len(),
                o.profile_id,
                o.object_type.len(),
                o.object_type,
                o.object_id.len(),
                o.object_id
            ),
        }
    }
    pub fn validate(&self) -> Result<()> {
        match self {
            Self::Sql(d) => d.validate(),
            Self::Object(o) => {
                document_field(&o.profile_id, 264)?;
                document_field(&o.object_type, 128)?;
                document_field(&o.object_id, 16 * 1024)?;
                document_field(&o.label, 1024)?;
                if o.pane > 4 {
                    return Err(StorageError::InvalidDocument);
                }
                document_field(&self.storage_id(), 16 * 1024 + 512)
            }
        }
    }
}
pub fn validate_workspace_tabs(snapshot: &WorkspaceSnapshot) -> Result<()> {
    if snapshot.tabs.len() > MAX_WORKSPACE_DOCUMENTS {
        return Err(StorageError::ResourceLimit);
    }
    if (!snapshot.tabs.is_empty() && snapshot.active_index >= snapshot.tabs.len())
        || (snapshot.tabs.is_empty() && snapshot.active_index != 0)
    {
        return Err(StorageError::InvalidDocument);
    }
    let mut ids = std::collections::HashSet::new();
    let mut total = 0usize;
    for tab in &snapshot.tabs {
        tab.validate()?;
        if !ids.insert(tab.storage_id()) {
            return Err(StorageError::InvalidDocument);
        }
        total = total.saturating_add(encoded_size(tab, MAX_RECORD_BYTES)?);
        if total > MAX_COLLECTION_BYTES {
            return Err(StorageError::ResourceLimit);
        }
    }
    Ok(())
}
fn bounded_data<'a>(
    row: &'a rusqlite::Row<'_>,
    column: usize,
    limit: usize,
    total: &mut usize,
) -> Result<&'a str> {
    let data = row
        .get_ref(column)?
        .as_str()
        .map_err(|_| StorageError::InvalidDocument)?;
    *total = total.saturating_add(data.len());
    if data.len() > limit || *total > MAX_COLLECTION_BYTES {
        return Err(StorageError::ResourceLimit);
    }
    Ok(data)
}
fn encoded_size(value: &impl Serialize, limit: usize) -> Result<usize> {
    struct Counter {
        count: usize,
        limit: usize,
    }
    impl std::io::Write for Counter {
        fn write(&mut self, bytes: &[u8]) -> std::io::Result<usize> {
            self.count = self.count.saturating_add(bytes.len());
            if self.count > self.limit {
                return Err(std::io::Error::other("metadata resource limit"));
            }
            Ok(bytes.len())
        }
        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }
    let mut counter = Counter { count: 0, limit };
    serde_json::to_writer(&mut counter, value).map_err(|_| StorageError::ResourceLimit)?;
    Ok(counter.count)
}
fn encode_bounded(value: &impl Serialize, limit: usize) -> Result<String> {
    let size = encoded_size(value, limit)?;
    let mut bytes = Vec::with_capacity(size);
    serde_json::to_writer(&mut bytes, value)?;
    String::from_utf8(bytes).map_err(|_| StorageError::InvalidDocument)
}

impl HistoryPolicy {
    pub fn validate(&self) -> Result<()> {
        if self.max_age_days == 0 || self.max_records == 0 {
            return Err(StorageError::InvalidRetention);
        }
        Ok(())
    }
}
/// Offset is a nonnegative u32; the requested page must remain bounded.
pub fn validate_history_page(limit: u32, _offset: u32) -> Result<()> {
    if limit > MAX_HISTORY_PAGE {
        return Err(StorageError::ResourceLimit);
    }
    Ok(())
}
