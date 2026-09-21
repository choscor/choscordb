//! PostgreSQL adapter with transaction-scoped server portals and bounded pages.
mod metadata;
mod object_data;
mod spool;
mod ssh;
mod worker;
use async_trait::async_trait;
use choscordb_driver_api::*;
use futures_util::StreamExt;
use postgres_native_tls::MakeTlsConnector;
use std::sync::{Arc, Mutex};
use tokio::sync::{mpsc, oneshot};
use tokio_postgres::tls::MakeTlsConnect;
fn display_notice(code: &str, message: &str) -> String {
    const MAX_BYTES: usize = 512;
    let mut output = String::with_capacity(MAX_BYTES);
    output.push('[');
    output.push_str(code);
    output.push_str("] ");
    let remaining = MAX_BYTES.saturating_sub(output.len());
    if message.len() <= remaining {
        output.push_str(message);
    } else {
        let mut end = remaining.saturating_sub('…'.len_utf8());
        while !message.is_char_boundary(end) {
            end -= 1;
        }
        output.push_str(&message[..end]);
        output.push('…');
    }
    output
}
pub struct PostgresDriver;
type Reply<T> = oneshot::Sender<Result<T>>;
struct Started {
    summary: QuerySummary,
    id: u32,
    columns: Vec<Column>,
    reader: Arc<dyn DeferredReader>,
}
struct Fetched {
    page: ResultPage,
    summary: QuerySummary,
}
enum Command {
    ObjectOpen(ObjectId, usize, Reply<object_data::Opened>),
    ObjectRead(object_data::Read, Reply<Fetched>),
    Execute(String, QueryOptions, usize, Reply<Started>),
    Fetch(u32, PageSize, usize, Reply<Fetched>),
    Finish(u32, Reply<()>),
    Metadata(Option<ObjectId>, Reply<Vec<SchemaObject>>),
    Ddl(ObjectId, Reply<String>),
    Edit(EditBatch, Reply<EditBatchSummary>),
    EditTarget(ObjectId, Reply<EditTarget>),
    EditQuery(String, Vec<String>, Reply<EditQueryTarget>),
    Transaction(bool, Reply<()>),
    Close(Reply<()>),
}
fn disconnected() -> DriverError {
    DriverError::new(ErrorKind::Disconnected, "PostgreSQL connection is closed")
}
pub(crate) fn normalize(error: tokio_postgres::Error) -> DriverError {
    if let Some(db) = error.as_db_error() {
        let code = db.code().code();
        let kind = if code == "57014" {
            ErrorKind::Cancelled
        } else if code.starts_with("28") {
            ErrorKind::Authentication
        } else if code.starts_with("08") {
            ErrorKind::Disconnected
        } else {
            ErrorKind::Query
        };
        DriverError::new(kind, db.message()).with_code(code)
    } else if error.is_closed() {
        disconnected()
    } else {
        DriverError::new(ErrorKind::Connection, "PostgreSQL communication failed")
    }
}
#[derive(Clone)]
struct Client(mpsc::Sender<Command>);
impl Client {
    async fn request<T>(&self, make: impl FnOnce(Reply<T>) -> Command) -> Result<T> {
        let (tx, rx) = oneshot::channel();
        self.0.try_send(make(tx)).map_err(|e| match e {
            mpsc::error::TrySendError::Full(_) => {
                DriverError::new(ErrorKind::ResourceLimit, "PostgreSQL command queue is full")
            }
            _ => disconnected(),
        })?;
        rx.await.map_err(|_| disconnected())?
    }
}
#[derive(Default)]
struct Generation {
    current: u64,
    cancelled: Option<u64>,
    running: bool,
}
#[derive(Default)]
struct Closing {
    requested: std::sync::atomic::AtomicBool,
    transport_aborted: std::sync::atomic::AtomicBool,
    changed: tokio::sync::Notify,
}
impl Closing {
    fn request(&self) {
        self.requested
            .store(true, std::sync::atomic::Ordering::Release);
        self.changed.notify_waiters();
    }
    async fn auxiliary<T>(
        &self,
        pump: &tokio::task::JoinHandle<()>,
        operation: impl std::future::Future<Output = Result<T>>,
    ) -> Result<T> {
        let changed = self.changed.notified();
        tokio::pin!(changed);
        changed.as_mut().enable();
        let stopped = async {
            if !self.requested.load(std::sync::atomic::Ordering::Acquire) {
                changed.await;
            }
        };
        tokio::select! {
            biased;
            _ = stopped => {
                // A CancelRequest can arrive before the server starts the query.
                // Closing the socket makes termination sticky across that race.
                self.transport_aborted.store(true, std::sync::atomic::Ordering::Release);
                pump.abort();
                Err(DriverError::new(ErrorKind::Disconnected, "Connection closed during metadata operation"))
            }
            result = operation => result,
        }
    }
}
struct Cancellation {
    closing: Closing,
    state: tokio::sync::Mutex<Generation>,
    next: std::sync::atomic::AtomicU64,
    token: tokio_postgres::CancelToken,
    tls: MakeTlsConnector,
    ssh: Option<SshTunnel>,
    ssh_secret: Option<Secret>,
    host: String,
    port: u16,
}
impl Cancellation {
    async fn send_cancel(&self) -> Result<()> {
        if let Some(settings) = &self.ssh {
            let mut stream =
                ssh::Stream::open(settings, self.ssh_secret.as_ref(), &self.host, self.port)?;
            let tls = <MakeTlsConnector as MakeTlsConnect<ssh::Stream>>::make_tls_connect(
                &mut self.tls.clone(),
                &self.host,
            )
            .map_err(|_| DriverError::new(ErrorKind::Tls, "Cannot initialize TLS"))?;
            // TLS shutdown can finish after writing only to the local pipe.
            // Keep the process alive while it drains those bytes to PostgreSQL.
            let mut process = stream.take_process();
            tokio::time::timeout(ssh::TIMEOUT, async {
                self.token
                    .cancel_query_raw(stream, tls)
                    .await
                    .map_err(normalize)?;
                process.finish().await
            })
            .await
            .map_err(|_| ssh::timeout_error())?
        } else {
            self.token
                .cancel_query(self.tls.clone())
                .await
                .map_err(normalize)
        }
    }
    async fn interrupt_current(&self) -> Result<()> {
        self.closing.request();
        let mut state = self.state.lock().await;
        state.cancelled = Some(state.current);
        if state.running {
            self.send_cancel().await?;
        }
        Ok(())
    }
}
struct QueryCancellation {
    shared: Arc<Cancellation>,
    generation: u64,
}
#[async_trait]
impl CancelHandle for QueryCancellation {
    async fn cancel(&self) -> Result<()> {
        let mut state = self.shared.state.lock().await;
        if state.current == self.generation {
            state.cancelled = Some(self.generation);
            if state.running {
                self.shared.send_cancel().await?;
            }
        } else if state.current.checked_add(1) == Some(self.generation) {
            state.cancelled = Some(self.generation);
        }
        Ok(())
    }
}
struct PostgresConnection {
    client: Client,
    cancel: Arc<Cancellation>,
}
struct Cursor {
    client: Client,
    id: u32,
    columns: Vec<Column>,
    reader: Arc<dyn DeferredReader>,
    summary: QuerySummary,
}
#[async_trait]
impl DatabaseDriver for PostgresDriver {
    fn id(&self) -> &'static str {
        "postgres"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities {
            schemas: true,
            transactions: true,
            native_cancellation: true,
            server_cursors: true,
            explain_plans: true,
            ddl: true,
            ..Default::default()
        }
    }
    async fn connect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>> {
        let ConnectionOptions::Postgres {
            host,
            port,
            database,
            user,
            password,
            ssh_secret,
            tls,
            root_certificate,
            ssh,
        } = options
        else {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "PostgreSQL options required",
            ));
        };
        if host.is_empty()
            || host.starts_with('/')
            || host.contains('\0')
            || port == 0
            || database.is_empty()
            || user.is_empty()
        {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "Invalid PostgreSQL connection settings",
            ));
        }
        if let Some(settings) = &ssh {
            settings.validate()?;
        }
        let root_certificate = if tls == TlsMode::Disable {
            None
        } else {
            root_certificate
        };
        let connector = tokio::task::spawn_blocking(move || {
            let mut builder = native_tls::TlsConnector::builder();
            if let Some(path) = root_certificate {
                use std::io::Read;
                const MAX_ROOT_BYTES: usize = 1024 * 1024;
                let io = |_| DriverError::new(ErrorKind::Tls, "Cannot read root certificate");
                let file = std::fs::File::open(path).map_err(io)?;
                let metadata = file.metadata().map_err(io)?;
                let limit = || {
                    DriverError::new(
                        ErrorKind::ResourceLimit,
                        "Root certificate exceeds 1 MiB limit",
                    )
                };
                if !metadata.is_file() || metadata.len() > MAX_ROOT_BYTES as u64 {
                    return Err(limit());
                }
                // Fixed capacity also bounds a file that grows after metadata inspection.
                let mut bytes = vec![0; MAX_ROOT_BYTES + 1];
                let mut reader = file.take((MAX_ROOT_BYTES + 1) as u64);
                let mut count = 0;
                while count < bytes.len() {
                    let read = reader.read(&mut bytes[count..]).map_err(io)?;
                    if read == 0 {
                        break;
                    }
                    count += read;
                }
                if count > MAX_ROOT_BYTES {
                    return Err(limit());
                }
                bytes.truncate(count);
                let cert = native_tls::Certificate::from_pem(&bytes)
                    .map_err(|_| DriverError::new(ErrorKind::Tls, "Invalid root certificate"))?;
                builder.add_root_certificate(cert);
            }
            builder
                .build()
                .map(MakeTlsConnector::new)
                .map_err(|_| DriverError::new(ErrorKind::Tls, "Cannot initialize TLS"))
        })
        .await
        .map_err(|_| DriverError::new(ErrorKind::Internal, "TLS worker failed"))??;
        let mut config = tokio_postgres::Config::new();
        config
            .host(&host)
            .port(port)
            .dbname(&database)
            .user(&user)
            .application_name("ChoscorDB")
            .connect_timeout(std::time::Duration::from_secs(15))
            .ssl_mode(if tls == TlsMode::Disable {
                tokio_postgres::config::SslMode::Disable
            } else {
                tokio_postgres::config::SslMode::Require
            });
        if let Some(password) = password {
            config.password(password.expose());
        }
        let connection_error = |error: tokio_postgres::Error| {
            if error.as_db_error().is_some() {
                normalize(error)
            } else {
                DriverError::new(
                    if tls == TlsMode::VerifyFull
                        && !std::error::Error::source(&error)
                            .is_some_and(|source| source.is::<std::io::Error>())
                    {
                        ErrorKind::Tls
                    } else {
                        ErrorKind::Connection
                    },
                    if ssh.is_some() {
                        "PostgreSQL connection through SSH failed; check SSH host trust, key or agent authentication, forwarding, and database TLS settings"
                    } else {
                        "PostgreSQL connection failed; check server, network, and TLS settings"
                    },
                )
            }
        };
        let (client, mut messages) = if let Some(settings) = &ssh {
            let stream = ssh::Stream::open(settings, ssh_secret.as_ref(), &host, port)?;
            let tls = <MakeTlsConnector as MakeTlsConnect<ssh::Stream>>::make_tls_connect(
                &mut connector.clone(),
                &host,
            )
            .map_err(|_| DriverError::new(ErrorKind::Tls, "Cannot initialize TLS"))?;
            let (client, mut connection) =
                tokio::time::timeout(ssh::TIMEOUT, config.connect_raw(stream, tls))
                    .await
                    .map_err(|_| ssh::timeout_error())?
                    .map_err(connection_error)?;
            (
                client,
                futures_util::stream::poll_fn(move |cx| connection.poll_message(cx)).boxed(),
            )
        } else {
            let (client, mut connection) = config
                .connect(connector.clone())
                .await
                .map_err(connection_error)?;
            (
                client,
                futures_util::stream::poll_fn(move |cx| connection.poll_message(cx)).boxed(),
            )
        };
        let notices = Arc::new(Mutex::new(Vec::new()));
        let incoming = notices.clone();
        let pump = tokio::spawn(async move {
            while let Some(message) = messages.next().await {
                match message {
                    Ok(tokio_postgres::AsyncMessage::Notice(notice)) => {
                        if let Ok(mut list) = incoming.lock()
                            && list.len() < 16
                        {
                            list.push(display_notice(notice.code().code(), notice.message()));
                        }
                    }
                    Err(_) => break,
                    _ => {}
                }
            }
        });
        let cancel = Arc::new(Cancellation {
            closing: Closing::default(),
            state: tokio::sync::Mutex::new(Generation::default()),
            next: std::sync::atomic::AtomicU64::new(1),
            token: client.cancel_token(),
            tls: connector,
            ssh,
            ssh_secret,
            host,
            port,
        });
        let (tx, rx) = mpsc::channel(32);
        tokio::spawn(worker::run(client, rx, cancel.clone(), notices, pump));
        Ok(Box::new(PostgresConnection {
            client: Client(tx),
            cancel,
        }))
    }
}
#[async_trait]
impl Connection for PostgresConnection {
    async fn inspect_edit_query(
        &mut self,
        sql: &str,
        result_columns: Vec<String>,
    ) -> Result<EditQueryTarget> {
        self.client
            .request(|r| Command::EditQuery(sql.into(), result_columns, r))
            .await
    }
    async fn inspect_edit_target(&mut self, object: &ObjectId) -> Result<EditTarget> {
        self.client
            .request(|r| Command::EditTarget(object.clone(), r))
            .await
    }
    async fn apply_edit_batch(&mut self, batch: EditBatch) -> Result<EditBatchSummary> {
        self.client.request(|r| Command::Edit(batch, r)).await
    }
    fn cancellation_handle(&self) -> Arc<dyn CancelHandle> {
        Arc::new(QueryCancellation {
            shared: self.cancel.clone(),
            generation: self.cancel.next.load(std::sync::atomic::Ordering::Acquire),
        })
    }
    async fn execute(&mut self, sql: &str, options: QueryOptions) -> Result<Box<dyn ResultCursor>> {
        self.execute_bounded(sql, options, 4 * 1024 * 1024).await
    }
    async fn execute_bounded(
        &mut self,
        sql: &str,
        options: QueryOptions,
        max: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        let start = self
            .client
            .request(|r| Command::Execute(sql.into(), options, max, r))
            .await?;
        Ok(Box::new(Cursor {
            client: self.client.clone(),
            id: start.id,
            columns: start.columns,
            reader: start.reader,
            summary: start.summary,
        }))
    }
    async fn open_object(
        &mut self,
        object: &ObjectId,
        max: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        object_data::open(self.client.clone(), self.cancel.clone(), object, max).await
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
        self.cancel.interrupt_current().await?;
        self.client.request(Command::Close).await
    }
}
impl Drop for PostgresConnection {
    fn drop(&mut self) {
        if let Ok(runtime) = tokio::runtime::Handle::try_current() {
            let cancel = self.cancel.clone();
            runtime.spawn(async move {
                let _ = cancel.interrupt_current().await;
            });
        }
        let (tx, _) = oneshot::channel();
        let _ = self.client.0.try_send(Command::Close(tx));
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
    async fn fetch_page_bounded(&mut self, size: PageSize, max: usize) -> Result<ResultPage> {
        let fetched = self
            .client
            .request(|r| Command::Fetch(self.id, size, max, r))
            .await?;
        self.summary = fetched.summary;
        Ok(fetched.page)
    }
    async fn load_value(&mut self, handle: Handle) -> Result<Value> {
        let reader = self.reader.clone();
        tokio::task::spawn_blocking(move || {
            let first = reader.read_chunk(handle, 0, MAX_VALUE_CHUNK_BYTES)?;
            if first.total_bytes > 64 * 1024 * 1024 {
                return Err(DriverError::new(
                    ErrorKind::ResourceLimit,
                    "Value exceeds detail limit",
                ));
            }
            let mut bytes = Vec::with_capacity(first.total_bytes as usize);
            bytes.extend(first.bytes);
            while bytes.len() < first.total_bytes as usize {
                bytes.extend(
                    reader
                        .read_chunk(handle, bytes.len() as u64, MAX_VALUE_CHUNK_BYTES)?
                        .bytes,
                );
            }
            if first.kind == DeferredKind::Text {
                String::from_utf8(bytes)
                    .map(Value::Text)
                    .map_err(|_| DriverError::new(ErrorKind::Query, "Invalid text encoding"))
            } else {
                Ok(Value::Binary(bytes))
            }
        })
        .await
        .map_err(|_| DriverError::new(ErrorKind::Internal, "Value worker failed"))?
    }
    fn retained_bytes_after_completion(&self) -> Option<usize> {
        let columns = std::mem::size_of::<Vec<Column>>()
            + self.columns.capacity() * std::mem::size_of::<Column>()
            + self
                .columns
                .iter()
                .map(|column| {
                    column.name.capacity()
                        + column.database_type.capacity()
                        + column.timezone.as_ref().map_or(0, String::capacity)
                })
                .sum::<usize>();
        let warnings = self.summary.warnings.capacity() * std::mem::size_of::<String>()
            + self
                .summary
                .warnings
                .iter()
                .map(String::capacity)
                .sum::<usize>();
        // Includes the cursor, a possible manual-transaction portal's schema,
        // worker completion/summary copies, and bounded connection/reader state.
        Some(64 * 1024 + std::mem::size_of::<Self>() + 2 * columns + 3 * warnings)
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

#[cfg(test)]
mod auxiliary_close_tests {
    use super::*;
    #[tokio::test]
    async fn close_before_auxiliary_poll_never_starts_operation() {
        let closing = Closing::default();
        let pump = tokio::spawn(std::future::pending::<()>());
        closing.request();
        let result = closing
            .auxiliary(&pump, async {
                panic!("closed operation was polled");
                #[allow(unreachable_code)]
                Ok::<_, DriverError>(())
            })
            .await;
        assert_eq!(result.unwrap_err().kind, ErrorKind::Disconnected);
        assert!(pump.await.unwrap_err().is_cancelled());
    }
    #[tokio::test]
    async fn close_wakes_already_pending_auxiliary_operation() {
        let closing = Arc::new(Closing::default());
        let (started, ready) = tokio::sync::oneshot::channel();
        let copy = closing.clone();
        let operation = tokio::spawn(async move {
            let pump = tokio::spawn(std::future::pending::<()>());
            let result = copy
                .auxiliary(&pump, async {
                    let _ = started.send(());
                    std::future::pending::<Result<()>>().await
                })
                .await;
            assert!(pump.await.unwrap_err().is_cancelled());
            result
        });
        ready.await.unwrap();
        closing.request();
        let result = tokio::time::timeout(std::time::Duration::from_secs(1), operation)
            .await
            .unwrap()
            .unwrap();
        assert_eq!(result.unwrap_err().kind, ErrorKind::Disconnected);
    }
}
