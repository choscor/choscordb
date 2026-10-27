//! MySQL and MariaDB adapters with disk-backed results and bounded pages.
mod connection;
mod editing;
mod flavor;
mod graph;
mod idle;
mod proxy;
mod row_codec;
mod runtime;
mod spool;
mod ssh;
mod tls;
use async_trait::async_trait;
/// The active MySQL transports of the process. An idle object connection is not active,
/// so it holds no permit. There is at most one for each connection.
static TRANSPORTS: std::sync::LazyLock<Arc<tokio::sync::Semaphore>> =
    std::sync::LazyLock::new(|| Arc::new(tokio::sync::Semaphore::new(8)));
fn reserve_transport() -> Result<Arc<tokio::sync::OwnedSemaphorePermit>> {
    TRANSPORTS.clone().try_acquire_owned().map(Arc::new).map_err(|_| error(ErrorKind::ResourceLimit, "MySQL native transport budget is full; close an unused MySQL connection or object tab"))
}
use choscordb_driver_api::*;
use flavor::Flavor;
use mysql_async::{Conn, Opts, OptsBuilder, prelude::Queryable};
use std::{
    io::{Read, Seek, SeekFrom, Write},
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
    time::Duration,
};

#[derive(Default)]
pub struct MysqlDriver;
impl MysqlDriver {
    pub fn new() -> Self {
        Self
    }
}
#[derive(Default)]
pub struct MariadbDriver;
struct Cancellation {
    cancelled: AtomicBool,
    changed: tokio::sync::Notify,
}
impl Cancellation {
    fn new() -> Arc<Self> {
        Arc::new(Self {
            cancelled: AtomicBool::new(false),
            changed: tokio::sync::Notify::new(),
        })
    }
}
#[async_trait]
impl CancelHandle for Cancellation {
    async fn cancel(&self) -> Result<()> {
        self.cancelled.store(true, Ordering::Release);
        self.changed.notify_waiters();
        Ok(())
    }
}
/// An object-data connection that waits for the next `open_object`. It holds no
/// transport permit, so an idle connection does not use the budget of open tabs.
struct IdleObject {
    conn: Conn,
}
/// The auxiliary object-data connection of one `MysqlConnection`. It closes with
/// that connection.
#[derive(Default)]
struct ObjectSlot {
    idle: Option<IdleObject>,
    closed: bool,
}
type Objects = Arc<std::sync::Mutex<ObjectSlot>>;
fn objects_unavailable() -> DriverError {
    error(ErrorKind::Internal, "MySQL object connection state failed")
}
/// Takes the idle object connection, or `None` when the slot is empty or closed.
fn take_idle_object(objects: &Objects) -> Result<Option<IdleObject>> {
    Ok(objects
        .lock()
        .map_err(|_| objects_unavailable())?
        .idle
        .take())
}
/// Closes the slot and its idle connection.
fn close_objects(objects: &Objects) {
    let idle = match objects.lock() {
        Ok(mut slot) => {
            slot.closed = true;
            slot.idle.take()
        }
        // A poisoned slot still owns its connection; close it too.
        Err(poisoned) => {
            let mut slot = poisoned.into_inner();
            slot.closed = true;
            slot.idle.take()
        }
    };
    if let Some(idle) = idle {
        drop(connection::PendingConnection::new(idle.conn));
    }
}
/// What another session of the same Connect needs: the settings and the transport.
#[derive(Clone)]
struct Sessions {
    flavor: Flavor,
    opts: Opts,
    prefer_tls: bool,
    connect_timeout: Duration,
    tunnel: Option<Arc<ssh::Tunnel>>,
    proxy: Option<Arc<proxy::Tunnel>>,
}
impl Sessions {
    async fn open_session(self) -> Result<MysqlConnection> {
        let transport = reserve_transport()?;
        let pending = connection::connect(self.opts.clone(), self.prefer_tls).await?;
        let idle = pending.1.clone();
        let mut conn = pending.ready();
        // The version only selects an optional notice, so an account limited to
        // changing its password still connects. Its session mode is then unknown.
        let (version, sql_mode): (Option<String>, Option<String>) = conn
            .query_first("SELECT VERSION(), @@SESSION.sql_mode")
            .await
            .unwrap_or(None)
            .unwrap_or_default();
        let notices = self
            .flavor
            .mismatch_notice(Flavor::of_server(version.as_deref().unwrap_or_default()))
            .into_iter()
            .map(String::from)
            .collect();
        Ok(MysqlConnection {
            flavor: self.flavor,
            notices,
            idle,
            connect_timeout: self.connect_timeout,
            conn: Some(conn),
            sql_mode,
            objects: Objects::default(),
            opts: self.opts,
            prefer_tls: self.prefer_tls,
            next: Cancellation::new(),
            pending: None,
            active: None,
            _tunnel: self.tunnel,
            _proxy: self.proxy,
            transport: Some(transport),
        })
    }
}
#[async_trait]
impl SessionOpener for Sessions {
    async fn open(&self) -> Result<Box<dyn Connection>> {
        let session = tokio::time::timeout(self.connect_timeout, self.clone().open_session())
            .await
            .map_err(|_| DriverError::new(ErrorKind::Timeout, "MySQL connection timed out"))??;
        Ok(Box::new(session))
    }
}
struct MysqlConnection {
    flavor: Flavor,
    notices: Vec<String>,
    idle: idle::Tracker,
    connect_timeout: Duration,
    conn: Option<Conn>,
    // The session mode, set at connect and after each statement; `None` when unknown.
    sql_mode: Option<String>,
    objects: Objects,
    opts: Opts,
    prefer_tls: bool,
    next: Arc<Cancellation>,
    pending: Option<tokio::task::JoinHandle<Option<runtime::Finished>>>,
    active: Option<Arc<Cancellation>>,
    _tunnel: Option<Arc<ssh::Tunnel>>,
    _proxy: Option<Arc<proxy::Tunnel>>,
    transport: Option<Arc<tokio::sync::OwnedSemaphorePermit>>,
}
fn error(kind: ErrorKind, message: &str) -> DriverError {
    DriverError::new(kind, message)
}
fn closed() -> DriverError {
    error(ErrorKind::Disconnected, "MySQL connection is closed")
}
fn normalize(e: mysql_async::Error) -> DriverError {
    match e {
        mysql_async::Error::Io(mysql_async::IoError::Tls(_)) => {
            error(ErrorKind::Tls, "MySQL TLS verification failed")
        }
        mysql_async::Error::Server(e) => DriverError::new(
            if e.code == 1045 {
                ErrorKind::Authentication
            } else {
                ErrorKind::Query
            },
            e.message,
        )
        .with_code(e.code.to_string()),
        mysql_async::Error::Driver(mysql_async::DriverError::UnknownAuthPlugin { name }) => {
            DriverError::new(
                ErrorKind::Authentication,
                if name == "parsec" {
                    "The server requires MariaDB parsec authentication, which is unsupported; use an account with mysql_native_password or ed25519".to_owned()
                } else {
                    format!("The server requires the unsupported {name} authentication plugin")
                },
            )
        }
        _ => error(
            ErrorKind::Connection,
            "MySQL communication failed; check server, network, and TLS settings",
        ),
    }
}
fn io_error(_: std::io::Error) -> DriverError {
    error(ErrorKind::Internal, "MySQL result storage failed")
}
fn limit() -> DriverError {
    error(
        ErrorKind::ResourceLimit,
        "MySQL result exceeds memory budget",
    )
}
fn capabilities() -> DriverCapabilities {
    DriverCapabilities {
        schemas: true,
        transactions: true,
        ddl: true,
        explain_plans: true,
        native_cancellation: true,
        editable_results: true,
        multiple_result_sets: true,
        stored_procedures: true,
        ..Default::default()
    }
}
#[async_trait]
impl DatabaseDriver for MysqlDriver {
    fn id(&self) -> &'static str {
        Flavor::Mysql.id()
    }
    fn capabilities(&self) -> DriverCapabilities {
        capabilities()
    }
    async fn connect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>> {
        connect_native(Flavor::Mysql, options, false).await
    }
    async fn reconnect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>> {
        connect_native(Flavor::Mysql, options, true).await
    }
}
#[async_trait]
impl DatabaseDriver for MariadbDriver {
    fn id(&self) -> &'static str {
        Flavor::Mariadb.id()
    }
    fn capabilities(&self) -> DriverCapabilities {
        capabilities()
    }
    async fn connect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>> {
        connect_native(Flavor::Mariadb, options, false).await
    }
    async fn reconnect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>> {
        connect_native(Flavor::Mariadb, options, true).await
    }
}
async fn connect_native(
    flavor: Flavor,
    options: ConnectionOptions,
    fresh: bool,
) -> Result<Box<dyn Connection>> {
    let ((
        Flavor::Mysql,
        ConnectionOptions::Mysql {
            proxy,
            proxy_secret,
            host,
            port,
            database,
            user,
            password,
            ssh_secret,
            ssh_jump_secrets,
            ssh_private_key,
            ssh_jump_private_keys,
            tls,
            root_certificate,
            tls_identity,
            ssh,
        },
    )
    | (
        Flavor::Mariadb,
        ConnectionOptions::Mariadb {
            proxy,
            proxy_secret,
            host,
            port,
            database,
            user,
            password,
            ssh_secret,
            ssh_jump_secrets,
            ssh_private_key,
            ssh_jump_private_keys,
            tls,
            root_certificate,
            tls_identity,
            ssh,
        },
    )) = (flavor, options)
    else {
        return Err(error(ErrorKind::InvalidInput, "MySQL options required"));
    };
    let unix_socket = host.starts_with('/');
    if unix_socket
        && (!cfg!(unix) || !matches!(tls, TlsMode::Disable | TlsMode::Prefer) || ssh.is_some())
    {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "Unix sockets require a local connection without explicit TLS",
        ));
    }
    let host = if unix_socket {
        host
    } else {
        choscordb_driver_api::tcp_host(&host)?.to_owned()
    };
    if host.is_empty()
        || host.contains('\0')
        || port == 0
        || user.contains('\0')
        || database.contains('\0')
    {
        return Err(error(
            ErrorKind::InvalidInput,
            "Invalid MySQL connection settings",
        ));
    }
    if let Some(proxy) = &proxy {
        proxy.validate_transport(&host, port, ssh.is_some())?;
        validate_socks_secret(proxy, proxy_secret.as_ref())?;
    }
    if let Some(settings) = &ssh {
        settings.validate()?;
    }
    let connect_timeout = ssh.as_ref().map_or(Duration::from_secs(15), |settings| {
        Duration::from_secs(u64::from(settings.options.connect_timeout_seconds))
    });
    // The budget includes queued TLS preparation, DNS, tunnel establishment, and authentication.
    tokio::time::timeout(connect_timeout, async move {
        let prefer_tls = tls == TlsMode::Prefer && !unix_socket;
        let ssl = tls::options(
            if unix_socket { TlsMode::Disable } else { tls },
            root_certificate,
            tls_identity,
        )
        .await?;
        if fresh && let Some(settings) = &ssh {
            SshForward::invalidate_shared_context(
                settings,
                ssh_secret.as_ref(),
                &ssh_jump_secrets,
                ssh_private_key.as_ref(),
                &ssh_jump_private_keys,
                &host,
                port,
            )
            .await?;
        }
        let tunnel = match ssh {
            Some(ssh) => Some(
                ssh::Tunnel::open(
                    &ssh,
                    ssh_secret.as_ref(),
                    &ssh_jump_secrets,
                    ssh_private_key.as_ref(),
                    &ssh_jump_private_keys,
                    &host,
                    port,
                )
                .await?,
            ),
            None => None,
        };
        let proxy = match proxy {
            Some(settings) => Some(
                proxy::Tunnel::open(settings, proxy_secret, host.clone(), port, connect_timeout)
                    .await?,
            ),
            None => None,
        };
        let forwarding_port = tunnel
            .as_ref()
            .map(|t| t.port())
            .or_else(|| proxy.as_ref().map(|p| p.port()));
        let builder = OptsBuilder::default()
            .ip_or_hostname(if unix_socket {
                "localhost".to_owned()
            } else {
                host.clone()
            })
            .socket(unix_socket.then_some(host))
            .tcp_port(forwarding_port.unwrap_or(port))
            .resolved_ips(
                tunnel
                    .as_ref()
                    .map(|t| vec![t.connect_address().ip()])
                    .or_else(|| {
                        proxy
                            .as_ref()
                            .map(|_| vec![std::net::Ipv4Addr::LOCALHOST.into()])
                    }),
            )
            .db_name((!database.is_empty()).then_some(database))
            .user(Some(user))
            .pass(password.as_ref().map(Secret::expose))
            .ssl_opts(ssl)
            .client_found_rows(true)
            .prefer_socket(false)
            // Direct TCP gets the PostgreSQL default keepalive time of two hours.
            // A tunnel or a proxy keeps its own link alive.
            .tcp_keepalive((!unix_socket && forwarding_port.is_none()).then_some(7_200_000u32))
            .max_allowed_packet(Some(64 * 1024 * 1024));
        let sessions = Sessions {
            flavor,
            opts: builder.into(),
            prefer_tls,
            connect_timeout,
            tunnel,
            proxy,
        };
        Ok(Box::new(sessions.open_session().await?) as Box<dyn Connection>)
    })
    .await
    .map_err(|_| DriverError::new(ErrorKind::Timeout, "MySQL connection timed out"))?
}
fn columns(input: &[mysql_async::Column], max: usize) -> Result<Vec<Column>> {
    let bytes = input
        .iter()
        .try_fold(std::mem::size_of::<Vec<Column>>(), |n, c| {
            n.checked_add(std::mem::size_of::<Column>() + c.name_ref().len() * 3 + 64)
        })
        .ok_or_else(limit)?;
    if bytes > max {
        return Err(limit());
    }
    Ok(input
        .iter()
        .map(|c| Column {
            name: c.name_str().into_owned(),
            database_type: format!("{:?}", c.column_type())
                .trim_start_matches("MYSQL_TYPE_")
                .to_owned(),
            precision: None,
            scale: None,
            timezone: None,
            nullable: Some(
                !c.flags()
                    .contains(mysql_async::consts::ColumnFlags::NOT_NULL_FLAG),
            ),
        })
        .collect())
}
fn value(v: mysql_async::Value, c: &mysql_async::Column, json: bool) -> Result<Value> {
    use mysql_async::{Value as M, consts::ColumnType as T};
    Ok(match v {
        M::NULL => Value::Null,
        M::Int(n) => Value::Integer(n),
        M::UInt(n) => i64::try_from(n)
            .map(Value::Integer)
            .unwrap_or_else(|_| Value::Decimal(n.to_string())),
        M::Float(n) => Value::Real(n.into()),
        M::Double(n) => Value::Real(n),
        M::Bytes(b) => {
            if c.character_set() == 63
                && !matches!(
                    c.column_type(),
                    T::MYSQL_TYPE_DECIMAL | T::MYSQL_TYPE_NEWDECIMAL | T::MYSQL_TYPE_JSON
                )
            {
                Value::Binary(b)
            } else {
                let s = String::from_utf8(b)
                    .map_err(|_| error(ErrorKind::Query, "Invalid UTF-8 in MySQL value"))?;
                match c.column_type() {
                    T::MYSQL_TYPE_DECIMAL | T::MYSQL_TYPE_NEWDECIMAL => Value::Decimal(s),
                    T::MYSQL_TYPE_JSON => Value::Json(s),
                    _ if json => Value::Json(s),
                    _ => Value::Text(s),
                }
            }
        }
        M::Date(y, m, d, h, min, s, micros) => {
            if c.column_type() == T::MYSQL_TYPE_DATE {
                Value::Date(format!("{y:04}-{m:02}-{d:02}"))
            } else {
                Value::Timestamp(format_mysql_timestamp(y, m, d, h, min, s, micros))
            }
        }
        M::Time(negative, days, h, m, s, micros) => {
            Value::Time(format_mysql_time(negative, days, h, m, s, micros))
        }
    })
}

fn format_mysql_timestamp(y: u16, m: u8, d: u8, h: u8, min: u8, s: u8, micros: u32) -> String {
    format!("{y:04}-{m:02}-{d:02} {h:02}:{min:02}:{s:02}.{micros:06}")
}

fn format_mysql_time(negative: bool, days: u32, h: u8, m: u8, s: u8, micros: u32) -> String {
    format!(
        "{}{:02}:{m:02}:{s:02}.{micros:06}",
        if negative { "-" } else { "" },
        days as u64 * 24 + h as u64
    )
}
fn transaction_active(conn: &Conn) -> Option<bool> {
    conn.last_ok_packet().map(|p| {
        p.status_flags()
            .contains(mysql_async::consts::StatusFlags::SERVER_STATUS_IN_TRANS)
    })
}
impl MysqlConnection {
    async fn connection(&mut self) -> Result<&mut Conn> {
        if let Some(task) = self.pending.take() {
            let finished = task
                .await
                .map_err(|_| error(ErrorKind::Internal, "MySQL worker failed"))?;
            self.sql_mode = finished.as_ref().and_then(|f| f.sql_mode.clone());
            self.conn = finished.map(|f| f.conn);
            self.active = None;
        }
        self.conn.as_mut().ok_or_else(closed)
    }
}
#[async_trait]
impl Connection for MysqlConnection {
    fn sibling_sessions(&self) -> Option<Arc<dyn SessionOpener>> {
        Some(Arc::new(Sessions {
            flavor: self.flavor,
            opts: self.opts.clone(),
            prefer_tls: self.prefer_tls,
            connect_timeout: self.connect_timeout,
            tunnel: self._tunnel.clone(),
            proxy: self._proxy.clone(),
        }))
    }
    fn notices(&self) -> Vec<String> {
        self.notices.clone()
    }
    async fn inspect_result_cells(
        &mut self,
        object: Option<&ObjectId>,
        sql: &str,
        result_columns: Vec<String>,
    ) -> Result<Vec<ResultCellMetadata>> {
        let flavor = self.flavor;
        editing::result_cells(
            self.connection().await?,
            flavor,
            object,
            sql,
            result_columns,
        )
        .await
    }
    async fn idle_transaction_state(&mut self) -> Result<Option<IdleTransactionState>> {
        let idle = self.idle.clone();
        idle.reconcile(self.connection().await?);
        Ok(idle.snapshot())
    }

    async fn transaction_state(&mut self) -> Result<Option<bool>> {
        Ok(transaction_active(self.connection().await?))
    }

    fn cancellation_handle(&self) -> Arc<dyn CancelHandle> {
        self.next.clone()
    }
    async fn execute_bounded(
        &mut self,
        sql: &str,
        options: QueryOptions,
        max: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        if max < std::mem::size_of::<Vec<Column>>() {
            return Err(error(
                ErrorKind::ResourceLimit,
                "Result schema exceeds memory budget",
            ));
        }
        let cancel = std::mem::replace(&mut self.next, Cancellation::new());
        if cancel.cancelled.load(Ordering::Acquire) {
            return Err(error(ErrorKind::Cancelled, "MySQL query cancelled"));
        }
        self.connection().await?;
        let conn = self.conn.take().ok_or_else(closed)?;
        let (cursor, task) = runtime::start(
            conn,
            self.opts.clone(),
            self.prefer_tls,
            runtime::Start {
                flavor: self.flavor,
                sql: sql.to_owned(),
                options,
                max,
                expected: None,
                cancel: cancel.clone(),
                independent: false,
                idle: self.idle.clone(),
                spool: spool::Spool::new()?,
                transport: self.transport.as_ref().ok_or_else(closed)?.clone(),
                tunnel: self._tunnel.clone(),
                proxy: self._proxy.clone(),
            },
        )
        .await?;
        self.pending = Some(task);
        self.active = Some(cancel);
        cursor.map(|c| Box::new(c) as Box<dyn ResultCursor>)
    }
    async fn inspect_edit_target(&mut self, object: &ObjectId) -> Result<EditTarget> {
        let flavor = self.flavor;
        editing::inspect_target(self.connection().await?, flavor, object).await
    }
    async fn inspect_edit_query(
        &mut self,
        sql: &str,
        result_columns: Vec<String>,
    ) -> Result<EditQueryTarget> {
        let flavor = self.flavor;
        editing::inspect_query(self.connection().await?, flavor, sql, result_columns).await
    }
    async fn apply_edit_batch(&mut self, batch: EditBatch) -> Result<EditBatchSummary> {
        editing::apply_batch(self.connection().await?, batch).await
    }
    /// A session changes its mode only through its own statements, so a known mode
    /// needs no probe.
    async fn sql_mode(&mut self) -> Result<Option<String>> {
        self.connection().await?;
        if self.sql_mode.is_none() {
            self.sql_mode = self
                .conn
                .as_mut()
                .ok_or_else(closed)?
                .query_first("SELECT @@SESSION.sql_mode")
                .await
                .map_err(normalize)?;
        }
        Ok(self.sql_mode.clone())
    }
    async fn open_object(
        &mut self,
        object: &ObjectId,
        time: &ObjectDataTime,
        max: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        if self.conn.is_none() && self.pending.is_none() {
            return Err(closed());
        }
        if self.flavor == Flavor::Mysql {
            time.require_current()?;
        }
        let (db, table) = parse_table(object)?;
        // Reuse the idle object connection. A transport failure on it, for example
        // after the server closed it, opens a new connection. A server error is final.
        let transport = reserve_transport()?;
        let mut reused = None;
        if let Some(idle) = take_idle_object(&self.objects)? {
            let mut conn = connection::PendingConnection::new(idle.conn);
            match describe_object(&mut conn, &db, &table, time, max).await {
                Ok(described) => reused = Some((conn, described)),
                Err(error) if error.vendor_code.is_some() => return Err(error),
                Err(_) => {}
            }
        }
        let (conn, (transaction_started, sql, schema)) = match reused {
            Some(reused) => reused,
            None => {
                let mut conn = tokio::time::timeout(
                    self.connect_timeout,
                    connection::connect(self.opts.clone(), self.prefer_tls),
                )
                .await
                .map_err(|_| {
                    error(
                        ErrorKind::Timeout,
                        "MySQL object connection initialization timed out",
                    )
                })??;
                let described = describe_object(&mut conn, &db, &table, time, max).await?;
                (conn, described)
            }
        };
        Ok(Box::new(ObjectCursor {
            flavor: self.flavor,
            transaction_started,
            objects: self.objects.clone(),
            conn: Some(conn.ready()),
            sql,
            columns: schema,
            max,
            cancel: Cancellation::new(),
            inner: None,
            opts: self.opts.clone(),
            prefer_tls: self.prefer_tls,
            task: None,
            _tunnel: self._tunnel.clone(),
            _proxy: self._proxy.clone(),
            transport: Some(transport),
            spool: spool::Spool::new()?,
        }))
    }

    async fn load_metadata(&mut self, parent: Option<ObjectId>) -> Result<Vec<SchemaObject>> {
        let mut objects = Vec::new();
        let mut offset = 0;
        loop {
            let page = self
                .load_metadata_page(parent.clone(), offset, 1000)
                .await?;
            objects.extend(page.objects);
            match page.next_offset {
                Some(next) => offset = next,
                None => break,
            }
        }
        Ok(objects)
    }
    async fn load_object_graph(&mut self, object: &ObjectId) -> Result<ObjectGraph> {
        let flavor = self.flavor;
        tokio::time::timeout(
            Duration::from_secs(15),
            graph::object_graph(self.connection().await?, flavor, object),
        )
        .await
        .map_err(|_| error(ErrorKind::Timeout, "MySQL ER diagram metadata timed out"))?
    }
    async fn load_metadata_page(
        &mut self,
        parent: Option<ObjectId>,
        offset: u64,
        page_size: u32,
    ) -> Result<MetadataPage> {
        if page_size == 0 || page_size > 10000 {
            return Err(error(ErrorKind::InvalidInput, "Invalid metadata page size"));
        }
        let flavor = self.flavor;
        let mut objects = metadata(
            self.connection().await?,
            flavor,
            parent,
            offset,
            u64::from(page_size) + 1,
        )
        .await?;
        let more = objects.len() > page_size as usize;
        objects.truncate(page_size as usize);
        Ok(MetadataPage {
            objects,
            next_offset: if more {
                Some(offset.checked_add(u64::from(page_size)).ok_or_else(limit)?)
            } else {
                None
            },
        })
    }
    async fn object_ddl(&mut self, object: &ObjectId) -> Result<String> {
        let (db, table) = parse_table(object)?;
        let row: Option<mysql_async::Row> = self
            .connection()
            .await?
            .query_first(format!(
                "SHOW CREATE TABLE {}.{}",
                quote(&db),
                quote(&table)
            ))
            .await
            .map_err(normalize)?;
        row.and_then(|r| r.get(1))
            .ok_or_else(|| error(ErrorKind::Query, "MySQL object DDL is unavailable"))
    }
    async fn commit(&mut self) -> Result<()> {
        let idle = self.idle.clone();
        let connection = self.connection().await?;
        let result = connection.query_drop("COMMIT").await.map_err(normalize);
        idle.reconcile(connection);
        result
    }
    async fn rollback(&mut self) -> Result<()> {
        let idle = self.idle.clone();
        let connection = self.connection().await?;
        let result = connection.query_drop("ROLLBACK").await.map_err(normalize);
        idle.reconcile(connection);
        result
    }
    async fn close(&mut self) -> Result<()> {
        if let Some(cancel) = &self.active {
            cancel.cancel().await?;
        }
        let _ = self.connection().await;
        if let Some(conn) = self.conn.take() {
            drop(connection::PendingConnection::new(conn));
        }
        close_objects(&self.objects);
        self.transport.take();
        self._tunnel.take();
        self._proxy.take();
        Ok(())
    }
}
/// Whether a transaction was open, the object read and its columns.
async fn describe_object(
    conn: &mut connection::PendingConnection,
    db: &str,
    table: &str,
    time: &ObjectDataTime,
    max: usize,
) -> Result<(bool, String, Vec<Column>)> {
    // COM_STMT_PREPARE metadata may replace the last OK status flags.
    // It cannot change the live transaction, so retain its pre-prepare state.
    let transaction_started = transaction_active(conn.connection()).unwrap_or(false);
    let (sql, stmt) = tokio::time::timeout(Duration::from_secs(15), async {
        let sql = object_select(conn.connection(), db, table, time).await?;
        let stmt = conn.connection().prep(&sql).await.map_err(normalize)?;
        Ok::<_, DriverError>((sql, stmt))
    })
    .await
    .map_err(|_| error(ErrorKind::Timeout, "MySQL object metadata timed out"))??;
    let schema = columns(stmt.columns(), max)?;
    conn.connection().close(stmt).await.map_err(normalize)?;
    Ok((transaction_started, sql, schema))
}
/// Keeps a finished object connection for the next `open_object`.
fn park_object(objects: &Objects, conn: Conn) {
    let conn = connection::PendingConnection::new(conn);
    if let Ok(mut slot) = objects.lock()
        && !slot.closed
        && slot.idle.is_none()
    {
        slot.idle = Some(IdleObject { conn: conn.ready() });
    }
}
/// The object read for `time`. History reads add MariaDB's period columns,
/// which `*` omits while they are invisible.
async fn object_select(
    conn: &mut Conn,
    db: &str,
    table: &str,
    time: &ObjectDataTime,
) -> Result<String> {
    let relation = format!("{}.{}", quote(db), quote(table));
    let clause = match time {
        ObjectDataTime::Current => return Ok(format!("SELECT * FROM {relation}")),
        ObjectDataTime::AsOf(at) => format!("FOR SYSTEM_TIME AS OF TIMESTAMP '{}'", at.as_str()),
        ObjectDataTime::All => "FOR SYSTEM_TIME ALL".to_owned(),
    };
    let periods: Vec<(String, String)> = conn
        .exec(
            "SELECT COLUMN_NAME, EXTRA FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? AND GENERATION_EXPRESSION IN ('ROW START', 'ROW END') ORDER BY GENERATION_EXPRESSION DESC",
            (db, table),
        )
        .await
        .map_err(normalize)?;
    // Implicit period columns are absent from the catalog and always hidden.
    let extra: Vec<String> = if periods.is_empty() {
        vec!["ROW_START".into(), "ROW_END".into()]
    } else {
        periods
            .into_iter()
            .filter(|(_, extra)| extra.contains("INVISIBLE"))
            .map(|(name, _)| quote(&name))
            .collect()
    };
    Ok(format!(
        "SELECT *{} FROM {relation} {clause}",
        extra
            .iter()
            .map(|name| format!(", {name}"))
            .collect::<String>()
    ))
}
struct ObjectCursor {
    flavor: Flavor,
    transaction_started: bool,
    objects: Objects,
    conn: Option<Conn>,
    sql: String,
    columns: Vec<Column>,
    max: usize,
    cancel: Arc<Cancellation>,
    inner: Option<runtime::StreamCursor>,
    opts: Opts,
    prefer_tls: bool,
    task: Option<tokio::task::JoinHandle<Option<runtime::Finished>>>,
    spool: spool::Spool,
    _tunnel: Option<Arc<ssh::Tunnel>>,
    _proxy: Option<Arc<proxy::Tunnel>>,
    transport: Option<Arc<tokio::sync::OwnedSemaphorePermit>>,
}
#[async_trait]
impl ResultCursor for ObjectCursor {
    fn deferred_reader(&self) -> Option<Arc<dyn DeferredReader>> {
        Some(self.spool.reader())
    }
    fn independent_cancellation_handle(&self) -> Option<Arc<dyn CancelHandle>> {
        Some(self.cancel.clone())
    }
    fn columns(&self) -> &[Column] {
        &self.columns
    }
    async fn fetch_page_bounded(&mut self, size: PageSize, max: usize) -> Result<ResultPage> {
        if self.cancel.cancelled.load(Ordering::Acquire) {
            return Err(error(ErrorKind::Cancelled, "MySQL object read cancelled"));
        }
        if self.inner.is_none() {
            let mut conn = connection::PendingConnection::new(self.conn.take().ok_or_else(closed)?);
            // Starting another transaction would implicitly commit manual startup writes.
            if !self.transaction_started {
                conn.connection()
                    .query_drop("START TRANSACTION READ ONLY")
                    .await
                    .map_err(normalize)?;
            }
            let (cursor, task) = runtime::start(
                conn.ready(),
                self.opts.clone(),
                self.prefer_tls,
                runtime::Start {
                    flavor: self.flavor,
                    sql: self.sql.clone(),
                    options: QueryOptions::default(),
                    max: self.max,
                    expected: Some(self.columns.clone()),
                    cancel: self.cancel.clone(),
                    independent: true,
                    idle: Default::default(),
                    spool: self.spool.clone(),
                    // The cursor keeps the permit with the connection it may park.
                    transport: self.transport.clone().ok_or_else(closed)?,
                    tunnel: self._tunnel.clone(),
                    proxy: self._proxy.clone(),
                },
            )
            .await?;
            self.task = Some(task);
            self.inner = Some(cursor?);
        }
        self.inner
            .as_mut()
            .ok_or_else(closed)?
            .fetch_page_bounded(size, max)
            .await
    }
    fn summary(&self) -> QuerySummary {
        self.inner
            .as_ref()
            .map_or_else(QuerySummary::default, |c| c.summary())
    }
    async fn close(&mut self) -> Result<()> {
        self.cancel.cancel().await?;
        if let Some(cursor) = &mut self.inner {
            cursor.close().await?;
        }
        // A worker that failed or was interrupted hands back no connection.
        let finished = match self.task.take() {
            Some(task) => task.await.unwrap_or(None).map(|finished| finished.conn),
            None => None,
        };
        let conn = finished.or_else(|| self.conn.take());
        // The closed tab gives back its transport permit.
        self.transport = None;
        match conn {
            // A user transaction stays with this connection, so it cannot serve a later read.
            Some(conn) if !self.transaction_started => park_object(&self.objects, conn),
            Some(conn) => drop(connection::PendingConnection::new(conn)),
            None => {}
        }
        Ok(())
    }
}
impl Drop for ObjectCursor {
    fn drop(&mut self) {
        if let Some(connection) = self.conn.take() {
            drop(connection::PendingConnection::new(connection));
        }
    }
}
impl Drop for MysqlConnection {
    fn drop(&mut self) {
        if let Some(connection) = self.conn.take() {
            drop(connection::PendingConnection::new(connection));
        }
        close_objects(&self.objects);
        if let Some(cancel) = &self.active {
            cancel.cancelled.store(true, Ordering::Release);
            cancel.changed.notify_waiters();
        }
    }
}
fn read_page(
    file: &mut std::fs::File,
    mut position: u64,
    count: u64,
    index: u64,
    columns: usize,
    size: PageSize,
    max: usize,
) -> Result<(ResultPage, u64, u64)> {
    let mut page = ResultPage {
        index,
        rows: Vec::with_capacity(size.get() as usize),
        has_more: false,
    };
    if max < std::mem::size_of::<ResultPage>() {
        return Err(limit());
    }
    // The page estimate with only the used row slots, kept as the rows arrive.
    let mut used = std::mem::size_of::<ResultPage>();
    while position < count && page.rows.len() < size.get() as usize {
        let offset = file.stream_position().map_err(io_error)?;
        let mut bound = [0; 8];
        file.read_exact(&mut bound).map_err(io_error)?;
        let bound = u64::from_le_bytes(bound);
        // The bound covers the record bytes, the decoded values and the row slots,
        // before either the input or the decoded values are allocated.
        if bound > max.saturating_sub(used) as u64 {
            file.seek(SeekFrom::Start(offset)).map_err(io_error)?;
            if page.rows.is_empty() {
                return Err(limit());
            }
            break;
        }
        let mut len = [0; 8];
        file.read_exact(&mut len).map_err(io_error)?;
        let len = u64::from_le_bytes(len);
        if len > bound {
            return Err(limit());
        }
        let mut bytes = vec![0; len as usize];
        file.read_exact(&mut bytes).map_err(io_error)?;
        let row = row_codec::decode(&bytes, columns)?;
        used += std::mem::size_of::<Row>() + choscordb_driver_api::row_estimated_bytes(&row);
        page.rows.push(row);
        position += 1;
    }
    page.rows.shrink_to_fit();
    page.has_more = position < count;
    Ok((page, position, file.stream_position().map_err(io_error)?))
}
fn quote(s: &str) -> String {
    format!("`{}`", s.replace('`', "``"))
}
fn id(parts: &[&str]) -> ObjectId {
    ObjectId(serde_json::to_string(parts).expect("string encoding cannot fail"))
}
fn parse_table(object: &ObjectId) -> Result<(String, String)> {
    let parts: Vec<String> = serde_json::from_str(&object.0)
        .map_err(|_| error(ErrorKind::InvalidInput, "Invalid MySQL object"))?;
    if parts.len() != 2 {
        return Err(error(
            ErrorKind::InvalidInput,
            "A MySQL table or view is required",
        ));
    }
    Ok((parts[0].clone(), parts[1].clone()))
}
async fn metadata(
    conn: &mut Conn,
    flavor: Flavor,
    parent: Option<ObjectId>,
    offset: u64,
    count: u64,
) -> Result<Vec<SchemaObject>> {
    let mut out = Vec::new();
    if let Some(parent) = parent {
        let parts: Vec<String> = serde_json::from_str(&parent.0)
            .map_err(|_| error(ErrorKind::InvalidInput, "Invalid MySQL object"))?;
        if parts.len() == 1 {
            let rows: Vec<(String,String)> = conn.exec("SELECT TABLE_NAME, TABLE_TYPE FROM information_schema.TABLES WHERE TABLE_SCHEMA = ? ORDER BY TABLE_NAME LIMIT ? OFFSET ?", (&parts[0],count,offset)).await.map_err(normalize)?;
            for (name, table_type) in rows {
                let relation = flavor.relation(&table_type);
                out.push(SchemaObject {
                    id: id(&[&parts[0], &name]),
                    parent: Some(parent.clone()),
                    qualified_name: format!("{}.{}", quote(&parts[0]), quote(&name)),
                    name,
                    has_children: relation.kind != ObjectKind::Sequence,
                    kind: relation.kind,
                    column: None,
                    properties: relation
                        .subtype
                        .map(|subtype| MetadataProperty::available("Relation subtype", subtype))
                        .into_iter()
                        .collect(),
                });
            }
        } else if parts.len() == 2 {
            let rows: Vec<(String,String,String)> = conn.exec("SELECT COLUMN_NAME, COLUMN_TYPE, IS_NULLABLE FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? ORDER BY ORDINAL_POSITION LIMIT ? OFFSET ?", (&parts[0],&parts[1],count,offset)).await.map_err(normalize)?;
            for (name, typ, nullable) in rows {
                out.push(SchemaObject {
                    id: id(&[&parts[0], &parts[1], &name]),
                    parent: Some(parent.clone()),
                    qualified_name: format!(
                        "{}.{}.{}",
                        quote(&parts[0]),
                        quote(&parts[1]),
                        quote(&name)
                    ),
                    name: name.clone(),
                    kind: ObjectKind::Column,
                    has_children: false,
                    column: Some(Column {
                        name,
                        database_type: typ,
                        nullable: Some(nullable == "YES"),
                        precision: None,
                        scale: None,
                        timezone: None,
                    }),
                    properties: vec![],
                });
            }
        }
    } else {
        let names: Vec<String> = conn.exec("SELECT SCHEMA_NAME FROM information_schema.SCHEMATA ORDER BY SCHEMA_NAME LIMIT ? OFFSET ?", (count,offset)).await.map_err(normalize)?;
        for name in names {
            out.push(SchemaObject {
                id: id(&[&name]),
                parent: None,
                qualified_name: quote(&name),
                name,
                kind: ObjectKind::Schema,
                has_children: true,
                column: None,
                properties: vec![],
            });
        }
    }
    Ok(out)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn identifies_mysql_and_advertises_supported_features() {
        assert_eq!(MysqlDriver.id(), "mysql");
        assert!(MysqlDriver.capabilities().transactions);
        assert!(!MysqlDriver.capabilities().server_cursors);
    }

    #[test]
    fn identifies_mariadb_with_the_mysql_feature_set() {
        assert_eq!(MariadbDriver.id(), "mariadb");
        assert_eq!(
            serde_json::to_value(MariadbDriver.capabilities()).unwrap(),
            serde_json::to_value(MysqlDriver.capabilities()).unwrap()
        );
    }

    #[tokio::test]
    async fn each_flavor_accepts_only_its_own_options() {
        let options = |mariadb: bool| {
            let (host, port, database, user) =
                ("127.0.0.1".into(), 1, String::new(), String::new());
            if mariadb {
                ConnectionOptions::Mariadb {
                    proxy: None,
                    proxy_secret: None,
                    host,
                    port,
                    database,
                    user,
                    password: None,
                    ssh_secret: None,
                    ssh_jump_secrets: Default::default(),
                    ssh_private_key: None,
                    ssh_jump_private_keys: Default::default(),
                    tls: TlsMode::Disable,
                    root_certificate: None,
                    tls_identity: None,
                    ssh: None,
                }
            } else {
                ConnectionOptions::Mysql {
                    proxy: None,
                    proxy_secret: None,
                    host,
                    port,
                    database,
                    user,
                    password: None,
                    ssh_secret: None,
                    ssh_jump_secrets: Default::default(),
                    ssh_private_key: None,
                    ssh_jump_private_keys: Default::default(),
                    tls: TlsMode::Disable,
                    root_certificate: None,
                    tls_identity: None,
                    ssh: None,
                }
            }
        };
        let kind = |result: Result<Box<dyn Connection>>| result.err().map(|error| error.kind);
        assert_eq!(
            kind(MariadbDriver.connect(options(false)).await),
            Some(ErrorKind::InvalidInput)
        );
        assert_eq!(
            kind(MysqlDriver.connect(options(true)).await),
            Some(ErrorKind::InvalidInput)
        );
    }

    #[test]
    fn unsupported_authentication_plugins_are_authentication_errors() {
        let unknown = |name: &str| {
            normalize(mysql_async::Error::Driver(
                mysql_async::DriverError::UnknownAuthPlugin { name: name.into() },
            ))
        };
        let parsec = unknown("parsec");
        assert_eq!(parsec.kind, ErrorKind::Authentication);
        assert!(parsec.message.contains("parsec"), "{}", parsec.message);
        assert!(parsec.message.contains("mysql_native_password"));
        assert!(parsec.message.contains("ed25519"));
        let other = unknown("authentication_ldap_sasl");
        assert_eq!(other.kind, ErrorKind::Authentication);
        assert!(other.message.contains("authentication_ldap_sasl"));
        assert!(!other.message.contains("ed25519"));
    }

    #[test]
    fn formats_temporal_values_without_losing_mysql_duration_semantics() {
        assert_eq!(format_mysql_time(false, 0, 10, 30, 0, 0), "10:30:00.000000");
        assert_eq!(format_mysql_time(true, 2, 1, 2, 3, 4), "-49:02:03.000004");
        assert_eq!(
            format_mysql_timestamp(2026, 9, 21, 10, 30, 0, 0),
            "2026-09-21 10:30:00.000000"
        );
    }
}
