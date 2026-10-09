use crate::*;
use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use std::sync::Arc;

/// Default owned-memory budget for convenience execution and paging calls.
pub const DEFAULT_RESULT_MEMORY_BUDGET: usize = 4 * 1024 * 1024;

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
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub enum MetadataAvailability {
    #[default]
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
impl SchemaObject {
    /// Column metadata, reported only for column rows.
    pub fn column_metadata(&self) -> Option<&Column> {
        self.column
            .as_ref()
            .filter(|_| self.kind == ObjectKind::Column)
    }
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
/// Bounded, one-hop foreign-key neighborhood returned by native catalog metadata.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct ObjectGraph {
    pub availability: MetadataAvailability,
    pub reason: String,
    pub warnings: Vec<String>,
    pub tables: Vec<ObjectGraphTable>,
    pub edges: Vec<ObjectGraphEdge>,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ObjectGraphTable {
    pub id: ObjectId,
    pub qualified_name: String,
    pub columns: Vec<ObjectGraphColumn>,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ObjectGraphColumn {
    pub name: String,
    pub database_type: String,
    pub primary_key: bool,
    pub foreign_key: bool,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ObjectGraphEdge {
    pub id: String,
    pub source_id: ObjectId,
    pub target_id: ObjectId,
    pub source_columns: Vec<String>,
    pub target_columns: Vec<String>,
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
impl EditQueryTarget {
    /// One target column per result column, in result order. A result column the
    /// target does not own is read-only and is never a key.
    pub fn aligned_columns(&self) -> Vec<EditColumn> {
        self.source_columns
            .iter()
            .map(|name| {
                self.target
                    .columns
                    .iter()
                    .find(|column| &column.name == name)
                    .cloned()
                    .unwrap_or_else(|| EditColumn {
                        name: name.clone(),
                        database_type: String::new(),
                        nullable: true,
                        generated: true,
                        key: false,
                    })
            })
            .collect()
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct EditColumn {
    pub name: String,
    pub database_type: String,
    pub nullable: bool,
    pub generated: bool,
    pub key: bool,
}
impl EditColumn {
    /// Whether a duplicated row copies this column's value. Keys and generated or
    /// unnamed columns are left for the database to fill.
    pub fn duplicable(&self) -> bool {
        !self.key && !self.generated && !self.name.is_empty()
    }
}
/// Catalog-verified presentation metadata for one result column. Empty source or
/// target fields mean that no corresponding cell action can be offered.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct ResultCellMetadata {
    pub source_column: String,
    pub source_object: String,
    pub source_qualified_name: String,
    pub nullable: Option<bool>,
    pub boolean: bool,
    pub enum_choices: Vec<String>,
    pub fk_target_object: String,
    pub fk_target_qualified_name: String,
    pub fk_target_column: String,
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
/// Connections must supply schema-budget enforcement before query execution.
/// An implementation with only unbounded execution is incomplete:
///
/// ```compile_fail,E0046
/// use async_trait::async_trait;
/// use choscordb_driver_api::*;
/// use std::sync::Arc;
/// struct UnboundedConnection;
/// #[async_trait]
/// impl Connection for UnboundedConnection {
///     fn cancellation_handle(&self) -> Arc<dyn CancelHandle> { unimplemented!() }
///     async fn execute(&mut self, _: &str, _: QueryOptions) -> Result<Box<dyn ResultCursor>> {
///         unimplemented!()
///     }
///     async fn load_metadata(&mut self, _: Option<ObjectId>) -> Result<Vec<SchemaObject>> {
///         Ok(vec![])
///     }
///     async fn commit(&mut self) -> Result<()> { Ok(()) }
///     async fn rollback(&mut self) -> Result<()> { Ok(()) }
///     async fn close(&mut self) -> Result<()> { Ok(()) }
/// }
/// ```
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
    /// Read-only catalog inspection, independent of result edit eligibility.
    /// `object` identifies Object Data; otherwise `sql` identifies a SQL result.
    async fn inspect_result_cells(
        &mut self,
        _object: Option<&ObjectId>,
        _sql: &str,
        result_columns: Vec<String>,
    ) -> Result<Vec<ResultCellMetadata>> {
        Ok(vec![ResultCellMetadata::default(); result_columns.len()])
    }
    /// Execute the reviewed statements in one owned transaction.
    async fn apply_edit_batch(&mut self, _batch: EditBatch) -> Result<EditBatchSummary> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Editable results are unavailable",
        ))
    }
    /// Execute using the default schema budget. Adapters implement `execute_bounded`.
    async fn execute(&mut self, sql: &str, options: QueryOptions) -> Result<Box<dyn ResultCursor>> {
        self.execute_bounded(sql, options, DEFAULT_RESULT_MEMORY_BUDGET)
            .await
    }
    /// Limit owned result schema storage. Inspect borrowed metadata before
    /// allocating owned schema or executing writes. A budget smaller than the
    /// empty schema container must be rejected before dispatch, keeping the
    /// connection usable. Other resource-limit failures may invalidate a session.
    async fn execute_bounded(
        &mut self,
        sql: &str,
        options: QueryOptions,
        max_schema_bytes: usize,
    ) -> Result<Box<dyn ResultCursor>>;
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
    async fn load_object_graph(&mut self, _object: &ObjectId) -> Result<ObjectGraph> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "ER diagram metadata is unsupported",
        ))
    }
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
/// Every cursor must implement allocation-aware bounded paging explicitly.
/// A cursor implementing only the convenience method is incomplete:
///
/// ```compile_fail,E0046
/// use async_trait::async_trait;
/// use choscordb_driver_api::*;
/// struct UnboundedCursor;
/// #[async_trait]
/// impl ResultCursor for UnboundedCursor {
///     fn columns(&self) -> &[Column] { &[] }
///     async fn fetch_page(&mut self, _: PageSize) -> Result<ResultPage> {
///         Ok(ResultPage { index: 0, rows: vec![], has_more: false })
///     }
///     fn summary(&self) -> QuerySummary { QuerySummary::default() }
///     async fn close(&mut self) -> Result<()> { Ok(()) }
/// }
/// ```
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
    /// Fetch using the default page budget. Adapters implement `fetch_page_bounded`.
    async fn fetch_page(&mut self, size: PageSize) -> Result<ResultPage> {
        self.fetch_page_bounded(size, DEFAULT_RESULT_MEMORY_BUDGET)
            .await
    }
    /// Return a page whose owned allocations fit `max_bytes`, including vector
    /// capacities. Enforce the budget before allocation; one retained lookahead
    /// row must independently fit it. A budget smaller than an empty page must
    /// be rejected before fetching, allowing a retry with a larger budget. Other
    /// resource-limit failures may terminate the cursor.
    async fn fetch_page_bounded(&mut self, size: PageSize, max_bytes: usize) -> Result<ResultPage>;
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
