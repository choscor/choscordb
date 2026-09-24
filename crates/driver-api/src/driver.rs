use crate::*;
use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct DriverCapabilities {
    pub schemas: bool,
    pub transactions: bool,
    pub native_cancellation: bool,
    pub server_cursors: bool,
    pub multiple_result_sets: bool,
    pub explain_plans: bool,
    pub editable_results: bool,
    pub stored_procedures: bool,
    pub database_specific_objects: bool,
    pub ddl: bool,
}
#[derive(Clone, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct ObjectId(pub String);
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum ObjectKind {
    Database,
    Schema,
    Group,
    Table,
    View,
    Sequence,
    Function,
    Column,
    PrimaryKey,
    ForeignKey,
    UniqueKey,
    Index,
    Other,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum MetadataAvailability {
    Available,
    Unsupported,
    Unavailable,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct MetadataProperty {
    pub name: String,
    pub value: String,
    pub availability: MetadataAvailability,
    pub reason: String,
}
impl MetadataProperty {
    pub fn available(name: &str, value: impl ToString) -> Self {
        Self {
            name: name.into(),
            value: value.to_string(),
            availability: MetadataAvailability::Available,
            reason: String::new(),
        }
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SchemaObject {
    pub id: ObjectId,
    pub parent: Option<ObjectId>,
    pub name: String,
    pub qualified_name: String,
    pub kind: ObjectKind,
    pub has_children: bool,
    pub column: Option<Column>,
    #[serde(default)]
    pub properties: Vec<MetadataProperty>,
}
#[derive(Clone, Default, Serialize, Deserialize)]
pub struct QuerySummary {
    #[serde(default)]
    pub has_more_results: bool,
    #[serde(default)]
    pub sql_mode: Option<String>,
    /// Authoritative connection transaction state when this execution summary was
    /// captured. None means the adapter cannot report it; do not infer SQL keywords.
    /// This is a snapshot, not a live connection state after subsequent operations.
    #[serde(default)]
    pub transaction_active: Option<bool>,
    pub affected_rows: Option<u64>,
    pub warnings: Vec<String>,
}
#[derive(Clone, Debug, Default)]
pub struct MetadataPage {
    pub objects: Vec<SchemaObject>,
    pub next_offset: Option<u64>,
}

/// A statement and its values as shown in the edit review. Values are always bound.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct EditStatement {
    pub sql: String,
    pub params: Vec<Value>,
    /// Updates and deletes must affect exactly one original row.
    pub expected_rows: Option<u64>,
}
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct EditBatch {
    pub statements: Vec<EditStatement>,
}
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct EditBatchSummary {
    pub affected_rows: Vec<u64>,
}
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct EditTarget {
    pub qualified_name: String,
    pub parameter_style: String,
    pub columns: Vec<EditColumn>,
    pub key_columns: Vec<String>,
    pub reason: String,
}
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct EditQueryTarget {
    pub target: EditTarget,
    pub source_columns: Vec<String>,
    pub reason: String,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct EditColumn {
    pub name: String,
    pub database_type: String,
    pub nullable: bool,
    pub generated: bool,
    pub key: bool,
}
#[async_trait]
pub trait DatabaseDriver: Send + Sync {
    fn id(&self) -> &'static str;
    fn capabilities(&self) -> DriverCapabilities;
    async fn connect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>>;
    /// Open a fresh session after recovery invalidates a failed connection.
    async fn reconnect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>> {
        self.connect(options).await
    }
}
/// Acquire a fresh handle immediately before each execute. It targets that next
/// execution only; a retained token must never cancel a later execution.
/// The actor handles cancellation of queued commands before dispatch. Cancellation
/// cannot wait for the mutable connection lock.
#[async_trait]
pub trait CancelHandle: Send + Sync {
    async fn cancel(&self) -> Result<()>;
}
/// Local, monotonic information used only for opt-in idle write rollback.
/// Unknown state must never be treated as permission to roll back.
#[derive(Clone, Copy, Debug, Default)]
pub struct IdleTransactionState {
    pub active: bool,
    pub manual: bool,
    pub write_pending: bool,
    pub started_at: Option<std::time::Instant>,
}
#[async_trait]
pub trait Connection: Send {
    /// Does not issue SQL or change transaction state.
    async fn idle_transaction_state(&mut self) -> Result<Option<IdleTransactionState>> {
        Ok(None)
    }

    /// Local session transaction state; does not issue a database query.
    async fn transaction_state(&mut self) -> Result<Option<bool>> {
        Ok(None)
    }
    /// Begin an explicitly tracked transaction with native characteristics.
    async fn begin_transaction(
        &mut self,
        _characteristics: crate::TransactionCharacteristics,
    ) -> Result<()> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Explicit transaction characteristics are unavailable",
        ))
    }
    /// Finish and immediately replace the transaction, retaining its characteristics.
    async fn chain_transaction(&mut self, _commit: bool) -> Result<()> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Chained transactions are unavailable",
        ))
    }
    /// SQL-level keepalive on a session with no pending transaction.
    async fn ping(&mut self) -> Result<()> {
        tokio::time::timeout(std::time::Duration::from_secs(15), async {
            if self.transaction_state().await? != Some(false) {
                return Err(DriverError::new(
                    ErrorKind::Unsupported,
                    "SQL keepalive requires a session without a pending transaction",
                ));
            }
            let mut cursor = self
                .execute_bounded(
                    "SELECT 1",
                    QueryOptions {
                        auto_commit: true,
                        ..Default::default()
                    },
                    64 * 1024,
                )
                .await?;
            loop {
                while cursor
                    .fetch_page_bounded(PageSize::new(100).expect("valid page size"), 64 * 1024)
                    .await?
                    .has_more
                {}
                if !cursor.next_result_set().await? {
                    break;
                }
            }
            cursor.close().await?;
            if self.transaction_state().await? != Some(false) {
                return Err(DriverError::new(
                    ErrorKind::Internal,
                    "SQL keepalive left an unexpected transaction",
                ));
            }
            Ok(())
        })
        .await
        .map_err(|_| DriverError::new(ErrorKind::Timeout, "SQL keepalive timed out"))?
    }
    fn cancellation_handle(&self) -> Arc<dyn CancelHandle>;
    async fn inspect_edit_target(&mut self, _object: &ObjectId) -> Result<EditTarget> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Editable table metadata is unavailable",
        ))
    }
    async fn inspect_edit_query(
        &mut self,
        _sql: &str,
        _result_columns: Vec<String>,
    ) -> Result<EditQueryTarget> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Editable query results are unavailable",
        ))
    }
    /// Execute the reviewed statements in one owned transaction.
    async fn apply_edit_batch(&mut self, _batch: EditBatch) -> Result<EditBatchSummary> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Editable results are unavailable",
        ))
    }
    async fn execute(&mut self, sql: &str, options: QueryOptions) -> Result<Box<dyn ResultCursor>>;
    /// Limit owned result schema storage. Adapters MUST override this to check
    /// borrowed metadata before allocating or executing writes; this compatibility
    /// default validates only after execution and allocation.
    async fn execute_bounded(
        &mut self,
        sql: &str,
        options: QueryOptions,
        max_schema_bytes: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        let mut cursor = self.execute(sql, options).await?;
        let bytes =
            cursor
                .columns()
                .iter()
                .fold(std::mem::size_of::<Vec<Column>>(), |total, column| {
                    total
                        .saturating_add(std::mem::size_of::<Column>())
                        .saturating_add(column.name.capacity())
                        .saturating_add(column.database_type.capacity())
                        .saturating_add(column.timezone.as_ref().map_or(0, String::capacity))
                });
        if bytes > max_schema_bytes {
            cursor.close().await?;
            return Err(DriverError::new(
                ErrorKind::ResourceLimit,
                "Result schema exceeds memory budget",
            ));
        }
        Ok(cursor)
    }
    /// Opens a bounded read-only object source alongside the existing SQL cursor.
    /// Must neither release that cursor nor begin, commit, or roll back a transaction.
    async fn open_object(
        &mut self,
        _object: &ObjectId,
        _max_schema_bytes: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Separate object browsing is unavailable for this driver",
        ))
    }
    async fn load_metadata(&mut self, parent: Option<ObjectId>) -> Result<Vec<SchemaObject>>;
    async fn sql_mode(&mut self) -> Result<Option<String>> {
        Ok(None)
    }
    async fn load_metadata_page(
        &mut self,
        parent: Option<ObjectId>,
        offset: u64,
        limit: u32,
    ) -> Result<MetadataPage> {
        if limit == 0 || limit > 10_000 {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "Invalid metadata page size",
            ));
        }
        let objects = self.load_metadata(parent).await?;
        let start = usize::try_from(offset)
            .map_err(|_| DriverError::new(ErrorKind::InvalidInput, "Invalid metadata offset"))?;
        let end = start.saturating_add(limit as usize).min(objects.len());
        let next_offset = (end < objects.len()).then_some(end as u64);
        Ok(MetadataPage {
            objects: objects
                .into_iter()
                .skip(start)
                .take(limit as usize)
                .collect(),
            next_offset,
        })
    }

    async fn object_ddl(&mut self, _object: &ObjectId) -> Result<String> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "DDL is not supported",
        ))
    }
    async fn commit(&mut self) -> Result<()>;
    async fn rollback(&mut self) -> Result<()>;
    /// Ends the session without committing unfinished work. Abort live cursors
    /// and roll back uncommitted transactions before their finalizers can commit.
    /// The session must be terminal even when cleanup reports an error.
    async fn close(&mut self) -> Result<()>;
}
/// Independent disk-backed deferred value access, without a database connection.
/// Call reads and the final reader drop on a blocking worker. A reader keeps its
/// backing storage alive independently of cursor close or subsequent executions.
pub trait DeferredReader: Send + Sync {
    /// Read raw bytes; text chunks may split UTF-8 code points. `max_bytes` must
    /// be between 1 and MAX_VALUE_CHUNK_BYTES. Offset equal to total length
    /// returns an empty chunk; greater offsets are invalid. The returned offset
    /// must equal the requested offset, and bytes must not extend beyond total_bytes.
    /// Implementers must bound byte-vector capacity by max_bytes before allocation;
    /// a non-EOF successful read must return at least one byte.
    fn read_chunk(&self, handle: Handle, offset: u64, max_bytes: usize) -> Result<ValueChunk>;
}
#[async_trait]
pub trait ResultCursor: Send {
    async fn next_result_set(&mut self) -> Result<bool> {
        Ok(false)
    }
    /// Independent object sources own a cancellation handle that cannot cancel SQL.
    fn independent_cancellation_handle(&self) -> Option<Arc<dyn CancelHandle>> {
        None
    }

    fn deferred_reader(&self) -> Option<Arc<dyn DeferredReader>> {
        None
    }
    fn columns(&self) -> &[Column];
    async fn fetch_page(&mut self, size: PageSize) -> Result<ResultPage>;
    /// Return a page whose owned allocations fit `max_bytes`, including vector
    /// capacities. Adapters MUST enforce this before allocation for strong bounds;
    /// this compatibility default can only check after producing a page. One
    /// retained lookahead row must independently fit the same budget.
    async fn fetch_page_bounded(&mut self, size: PageSize, max_bytes: usize) -> Result<ResultPage> {
        let page = self.fetch_page(size).await?;
        if page.estimated_bytes() > max_bytes {
            return Err(DriverError::new(
                ErrorKind::ResourceLimit,
                "Result page exceeds memory budget",
            ));
        }
        Ok(page)
    }
    async fn load_value(&mut self, _handle: Handle) -> Result<Value> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Deferred values are not supported",
        ))
    }
    /// Conservative owned-memory bound after EOF, excluding native database
    /// working memory. Callers MUST use this only after a page has `has_more=false`.
    /// None means the original source reservation must remain held.
    fn retained_bytes_after_completion(&self) -> Option<usize> {
        None
    }
    fn summary(&self) -> QuerySummary;
    async fn close(&mut self) -> Result<()>;
}

impl std::fmt::Debug for QuerySummary {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("QuerySummary")
            .field("affected_rows", &self.affected_rows)
            .field("transaction_active", &self.transaction_active)
            .field("warning_count", &self.warnings.len())
            .finish()
    }
}
