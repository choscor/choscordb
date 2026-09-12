//! SQLite adapter. All SQLite and spool I/O runs on a dedicated bounded worker.
mod metadata;
mod spool;
mod worker;
use async_trait::async_trait;
use choscordb_driver_api::*;
use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};
use tokio::sync::{mpsc, oneshot};
pub struct SqliteDriver;
type Reply<T> = oneshot::Sender<Result<T>>;
struct Started {
    reader: Arc<dyn DeferredReader>,
    id: u32,
    columns: Vec<Column>,
    summary: QuerySummary,
}
enum Command {
    Execute(String, QueryOptions, usize, Reply<Started>),
    Fetch(u32, PageSize, usize, Reply<ResultPage>),
    Load(u32, Handle, Reply<Value>),
    Finish(u32, Reply<()>),
    Metadata(Option<ObjectId>, Reply<Vec<SchemaObject>>),
    Ddl(ObjectId, Reply<String>),
    Transaction(bool, Reply<()>),
    Close(Reply<()>),
}
fn disconnected() -> DriverError {
    DriverError::new(ErrorKind::Disconnected, "SQLite worker is closed")
}
pub(crate) fn normalize(e: rusqlite::Error) -> DriverError {
    let kind = match &e {
        rusqlite::Error::MultipleStatement => ErrorKind::Unsupported,
        rusqlite::Error::SqliteFailure(code, _)
            if code.code == rusqlite::ErrorCode::OperationInterrupted =>
        {
            ErrorKind::Cancelled
        }
        _ => ErrorKind::Query,
    };
    let mut error = DriverError::new(kind, e.to_string());
    if let rusqlite::Error::SqliteFailure(code, _) = e {
        error.vendor_code = Some(code.extended_code.to_string());
    }
    error
}
#[derive(Clone)]
struct Client(mpsc::Sender<Command>);
impl Client {
    async fn request<T>(&self, make: impl FnOnce(Reply<T>) -> Command) -> Result<T> {
        let (tx, rx) = oneshot::channel();
        self.0.try_send(make(tx)).map_err(|e| match e {
            mpsc::error::TrySendError::Full(_) => {
                DriverError::new(ErrorKind::ResourceLimit, "SQLite command queue is full")
            }
            _ => disconnected(),
        })?;
        rx.await.map_err(|_| disconnected())?
    }
}
#[derive(Default)]
struct CancellationState {
    generation: u64,
    cancelled_generation: Option<u64>,
}
struct Cancellation {
    generation: std::sync::Mutex<CancellationState>,
    interrupt: rusqlite::InterruptHandle,
    requested: Arc<AtomicBool>,
}
struct QueryCancellation {
    shared: Arc<Cancellation>,
    generation: u64,
}
#[async_trait]
impl CancelHandle for QueryCancellation {
    async fn cancel(&self) -> Result<()> {
        let mut state = self.shared.generation.lock().map_err(|_| disconnected())?;
        if state.generation == self.generation {
            state.cancelled_generation = Some(
                state
                    .cancelled_generation
                    .map_or(self.generation, |previous| previous.max(self.generation)),
            );
            self.shared.requested.store(true, Ordering::Release);
            self.shared.interrupt.interrupt();
        } else if state.generation.checked_add(1) == Some(self.generation) {
            // The execute command can still be queued. Remember its cancellation
            // without interrupting the previous generation's active cursor.
            state.cancelled_generation = Some(self.generation);
        }
        Ok(())
    }
}
struct SqliteConnection {
    client: Client,
    cancel: Arc<Cancellation>,
}
struct Cursor {
    reader: Arc<dyn DeferredReader>,
    client: Client,
    source_sql_bytes: usize,
    id: u32,
    columns: Vec<Column>,
    summary: QuerySummary,
}
#[async_trait]
impl DatabaseDriver for SqliteDriver {
    fn id(&self) -> &'static str {
        "sqlite"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities {
            transactions: true,
            native_cancellation: true,
            explain_plans: true,
            ddl: true,
            ..Default::default()
        }
    }
    async fn connect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>> {
        let ConnectionOptions::Sqlite { path, read_only } = options else {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "SQLite connection options required",
            ));
        };
        let (tx, rx) = mpsc::channel(32);
        let (ready_tx, ready_rx) = oneshot::channel();
        std::thread::Builder::new()
            .name("choscordb-sqlite".into())
            .spawn(move || worker::run(path, read_only, rx, ready_tx))
            .map_err(|_| DriverError::new(ErrorKind::Io, "Cannot start SQLite worker"))?;
        let cancel = ready_rx.await.map_err(|_| disconnected())??;
        Ok(Box::new(SqliteConnection {
            client: Client(tx),
            cancel,
        }))
    }
}
#[async_trait]
impl Connection for SqliteConnection {
    fn cancellation_handle(&self) -> Arc<dyn CancelHandle> {
        Arc::new(QueryCancellation {
            shared: self.cancel.clone(),
            generation: self
                .cancel
                .generation
                .lock()
                .map(|g| g.generation.saturating_add(1))
                .unwrap_or(u64::MAX),
        })
    }
    async fn execute(&mut self, sql: &str, options: QueryOptions) -> Result<Box<dyn ResultCursor>> {
        self.execute_bounded(sql, options, 4 * 1024 * 1024).await
    }
    async fn execute_bounded(
        &mut self,
        sql: &str,
        options: QueryOptions,
        max_schema_bytes: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        let start = self
            .client
            .request(|r| Command::Execute(sql.into(), options, max_schema_bytes, r))
            .await?;
        Ok(Box::new(Cursor {
            reader: start.reader,
            client: self.client.clone(),
            source_sql_bytes: sql.len(),
            id: start.id,
            columns: start.columns,
            summary: start.summary,
        }))
    }
    async fn load_metadata(&mut self, parent: Option<ObjectId>) -> Result<Vec<SchemaObject>> {
        self.client.request(|r| Command::Metadata(parent, r)).await
    }
    async fn object_ddl(&mut self, object: &ObjectId) -> Result<String> {
        self.client
            .request(|r| Command::Ddl(object.clone(), r))
            .await
    }
    async fn commit(&mut self) -> Result<()> {
        self.client.request(|r| Command::Transaction(true, r)).await
    }
    async fn rollback(&mut self) -> Result<()> {
        self.client
            .request(|r| Command::Transaction(false, r))
            .await
    }
    async fn close(&mut self) -> Result<()> {
        self.cancel.interrupt.interrupt();
        self.client.request(Command::Close).await
    }
}
#[async_trait]
impl ResultCursor for Cursor {
    fn deferred_reader(&self) -> Option<Arc<dyn DeferredReader>> {
        Some(self.reader.clone())
    }
    fn columns(&self) -> &[Column] {
        &self.columns
    }
    async fn fetch_page(&mut self, size: PageSize) -> Result<ResultPage> {
        self.fetch_page_bounded(size, 4 * 1024 * 1024).await
    }
    async fn fetch_page_bounded(&mut self, size: PageSize, max_bytes: usize) -> Result<ResultPage> {
        self.client
            .request(|r| Command::Fetch(self.id, size, max_bytes, r))
            .await
    }
    async fn load_value(&mut self, handle: Handle) -> Result<Value> {
        self.client
            .request(|r| Command::Load(self.id, handle, r))
            .await
    }
    fn retained_bytes_after_completion(&self) -> Option<usize> {
        Some(
            64 * 1024
                + self.source_sql_bytes
                + std::mem::size_of::<Self>()
                + self.columns.capacity() * std::mem::size_of::<Column>()
                + self
                    .columns
                    .iter()
                    .map(|column| {
                        column.name.capacity()
                            + column.database_type.capacity()
                            + column.timezone.as_ref().map_or(0, String::capacity)
                    })
                    .sum::<usize>(),
        )
    }
    fn summary(&self) -> QuerySummary {
        self.summary.clone()
    }
    async fn close(&mut self) -> Result<()> {
        self.client.request(|r| Command::Finish(self.id, r)).await
    }
}
impl Drop for Cursor {
    fn drop(&mut self) {
        let (tx, _) = oneshot::channel();
        let _ = self.client.0.try_send(Command::Finish(self.id, tx));
    }
}

impl Drop for SqliteConnection {
    fn drop(&mut self) {
        self.cancel.requested.store(true, Ordering::Release);
        self.cancel.interrupt.interrupt();
        let (reply, _) = oneshot::channel();
        let _ = self.client.0.try_send(Command::Close(reply));
    }
}
