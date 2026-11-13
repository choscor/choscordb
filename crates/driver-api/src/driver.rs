use crate::*;
use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use std::sync::Arc;

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

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct EditStatement {
    pub sql: String,
    pub params: Vec<Value>,
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
    pub fn duplicable(&self) -> bool {
        !self.key && !self.generated && !self.name.is_empty()
    }
}
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
    async fn reconnect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>> {
        self.connect(options).await
    }
}
#[async_trait]
pub trait CancelHandle: Send + Sync {
    async fn cancel(&self) -> Result<()>;
}
#[derive(Clone, Copy, Debug, Default)]
pub struct IdleTransactionState {
    pub active: bool,
    pub manual: bool,
    pub write_pending: bool,
    pub started_at: Option<std::time::Instant>,
}
#[async_trait]
pub trait SessionOpener: Send + Sync {
    async fn open(&self) -> Result<Box<dyn Connection>>;
}
#[async_trait]
pub trait Connection: Send {
    fn sibling_sessions(&self) -> Option<Arc<dyn SessionOpener>> {
        None
    }
    async fn idle_transaction_state(&mut self) -> Result<Option<IdleTransactionState>> {
        Ok(None)
    }

    async fn transaction_state(&mut self) -> Result<Option<bool>> {
        Ok(None)
    }
    async fn begin_transaction(
        &mut self,
        _characteristics: crate::TransactionCharacteristics,
    ) -> Result<()> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Explicit transaction characteristics are unavailable",
        ))
    }
    async fn chain_transaction(&mut self, _commit: bool) -> Result<()> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Chained transactions are unavailable",
        ))
    }
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
    fn notices(&self) -> Vec<String> {
        Vec::new()
    }
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
    async fn inspect_result_cells(
        &mut self,
        _object: Option<&ObjectId>,
        _sql: &str,
        result_columns: Vec<String>,
    ) -> Result<Vec<ResultCellMetadata>> {
        Ok(vec![ResultCellMetadata::default(); result_columns.len()])
    }
    async fn apply_edit_batch(&mut self, _batch: EditBatch) -> Result<EditBatchSummary> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Editable results are unavailable",
        ))
    }
    async fn execute(&mut self, sql: &str, options: QueryOptions) -> Result<Box<dyn ResultCursor>> {
        self.execute_bounded(sql, options, DEFAULT_RESULT_MEMORY_BUDGET)
            .await
    }
    async fn execute_bounded(
        &mut self,
        sql: &str,
        options: QueryOptions,
        max_schema_bytes: usize,
    ) -> Result<Box<dyn ResultCursor>>;
    async fn open_object(
        &mut self,
        _object: &ObjectId,
        _time: &ObjectDataTime,
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
    async fn close(&mut self) -> Result<()>;
}
pub trait DeferredReader: Send + Sync {
    fn read_chunk(&self, handle: Handle, offset: u64, max_bytes: usize) -> Result<ValueChunk>;
}
#[async_trait]
pub trait ResultCursor: Send {
    async fn next_result_set(&mut self) -> Result<bool> {
        Ok(false)
    }
    fn independent_cancellation_handle(&self) -> Option<Arc<dyn CancelHandle>> {
        None
    }

    fn deferred_reader(&self) -> Option<Arc<dyn DeferredReader>> {
        None
    }
    fn columns(&self) -> &[Column];
    async fn fetch_page(&mut self, size: PageSize) -> Result<ResultPage> {
        self.fetch_page_bounded(size, DEFAULT_RESULT_MEMORY_BUDGET)
            .await
    }
    async fn fetch_page_bounded(&mut self, size: PageSize, max_bytes: usize) -> Result<ResultPage>;
    async fn load_value(&mut self, _handle: Handle) -> Result<Value> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Deferred values are not supported",
        ))
    }
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
