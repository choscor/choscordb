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
    Table,
    View,
    Column,
    PrimaryKey,
    ForeignKey,
    UniqueKey,
    Index,
    Other,
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
}
#[derive(Clone, Default, Serialize, Deserialize)]
pub struct QuerySummary {
    /// Authoritative connection transaction state when this execution summary was
    /// captured. None means the adapter cannot report it; do not infer SQL keywords.
    /// This is a snapshot, not a live connection state after subsequent operations.
    #[serde(default)]
    pub transaction_active: Option<bool>,
    pub affected_rows: Option<u64>,
    pub warnings: Vec<String>,
}
#[async_trait]
pub trait DatabaseDriver: Send + Sync {
    fn id(&self) -> &'static str;
    fn capabilities(&self) -> DriverCapabilities;
    async fn connect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>>;
}
/// Acquire a fresh handle immediately before each execute. It targets that next
/// execution only; a retained token must never cancel a later execution.
/// The actor handles cancellation of queued commands before dispatch. Cancellation
/// cannot wait for the mutable connection lock.
#[async_trait]
pub trait CancelHandle: Send + Sync {
    async fn cancel(&self) -> Result<()>;
}
#[async_trait]
pub trait Connection: Send {
    fn cancellation_handle(&self) -> Arc<dyn CancelHandle>;
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
    async fn load_metadata(&mut self, parent: Option<ObjectId>) -> Result<Vec<SchemaObject>>;
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
