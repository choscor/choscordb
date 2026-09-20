//! MySQL adapter with disk-backed results and bounded pages.
mod editing;
mod runtime;
mod spool;
mod ssh;
use async_trait::async_trait;
static TRANSPORTS: std::sync::LazyLock<Arc<tokio::sync::Semaphore>> =
    std::sync::LazyLock::new(|| Arc::new(tokio::sync::Semaphore::new(8)));
fn reserve_transport() -> Result<Arc<tokio::sync::OwnedSemaphorePermit>> {
    TRANSPORTS.clone().try_acquire_owned().map(Arc::new).map_err(|_| error(ErrorKind::ResourceLimit, "MySQL native transport budget is full; close an unused MySQL connection or object tab"))
}
use choscordb_driver_api::*;
use mysql_async::{Conn, Opts, OptsBuilder, SslOpts, prelude::Queryable};
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
struct MysqlConnection {
    conn: Option<Conn>,
    opts: Opts,
    next: Arc<Cancellation>,
    pending: Option<tokio::task::JoinHandle<Option<Conn>>>,
    active: Option<Arc<Cancellation>>,
    _tunnel: Option<Arc<ssh::Tunnel>>,
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
#[async_trait]
impl DatabaseDriver for MysqlDriver {
    fn id(&self) -> &'static str {
        "mysql"
    }
    fn capabilities(&self) -> DriverCapabilities {
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
    async fn connect(&self, options: ConnectionOptions) -> Result<Box<dyn Connection>> {
        let ConnectionOptions::Mysql {
            host,
            port,
            database,
            user,
            password,
            tls,
            root_certificate,
            ssh,
        } = options
        else {
            return Err(error(ErrorKind::InvalidInput, "MySQL options required"));
        };
        if host.is_empty()
            || host.contains('\0')
            || host.starts_with('/')
            || port == 0
            || database.is_empty()
            || user.is_empty()
        {
            return Err(error(
                ErrorKind::InvalidInput,
                "Invalid MySQL connection settings",
            ));
        }
        let ssl = if tls == TlsMode::VerifyFull {
            let mut ssl = SslOpts::default();
            if let Some(path) = root_certificate {
                let bytes = tokio::task::spawn_blocking(move || -> Result<Vec<u8>> {
                    let file = std::fs::File::open(path)
                        .map_err(|_| error(ErrorKind::Tls, "Cannot read root certificate"))?;
                    if !file.metadata().map_err(io_error)?.is_file() {
                        return Err(error(
                            ErrorKind::Tls,
                            "Root certificate must be a regular file",
                        ));
                    }
                    let mut bytes = vec![0; 1024 * 1024 + 1];
                    let mut reader = file.take(bytes.len() as u64);
                    let mut count = 0;
                    while count < bytes.len() {
                        let n = reader.read(&mut bytes[count..]).map_err(io_error)?;
                        if n == 0 {
                            break;
                        }
                        count += n;
                    }
                    if count > 1024 * 1024 {
                        return Err(limit());
                    }
                    bytes.truncate(count);
                    Ok(bytes)
                })
                .await
                .map_err(|_| error(ErrorKind::Internal, "MySQL TLS worker failed"))??;
                ssl = ssl.with_root_certs(vec![bytes.into()]);
            }
            Some(ssl)
        } else {
            None
        };
        let tunnel = match ssh {
            Some(ssh) => Some(ssh::Tunnel::open(&ssh, &host, port).await?),
            None => None,
        };
        let opts: Opts = OptsBuilder::default()
            .ip_or_hostname(host)
            .tcp_port(tunnel.as_ref().map_or(port, |t| t.port()))
            .resolved_ips(
                tunnel
                    .as_ref()
                    .map(|_| vec![std::net::Ipv4Addr::LOCALHOST.into()]),
            )
            .db_name(Some(database))
            .user(Some(user))
            .pass(password.as_ref().map(Secret::expose))
            .ssl_opts(ssl)
            .client_found_rows(true)
            .prefer_socket(false)
            .max_allowed_packet(Some(64 * 1024 * 1024))
            .into();
        let transport = reserve_transport()?;
        let conn = tokio::time::timeout(Duration::from_secs(15), Conn::new(opts.clone()))
            .await
            .map_err(|_| error(ErrorKind::Connection, "MySQL connection timed out"))?
            .map_err(normalize)?;
        Ok(Box::new(MysqlConnection {
            conn: Some(conn),
            opts,
            next: Cancellation::new(),
            pending: None,
            active: None,
            _tunnel: tunnel,
            transport: Some(transport),
        }))
    }
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
fn value(v: mysql_async::Value, c: &mysql_async::Column) -> Result<Value> {
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
                    _ => Value::Text(s),
                }
            }
        }
        M::Date(y, m, d, h, min, s, micros) => {
            if c.column_type() == T::MYSQL_TYPE_DATE {
                Value::Date(format!("{y:04}-{m:02}-{d:02}"))
            } else {
                Value::Timestamp(format!(
                    "{y:04}-{m:02}-{d:02} {h:02}:{min:02}:{s:02}.{micros:06}"
                ))
            }
        }
        M::Time(negative, days, h, m, s, micros) => Value::Time(format!(
            "{}{:02}:{m:02}:{s:02}.{micros:06}",
            if negative { "-" } else { "" },
            days as u64 * 24 + h as u64
        )),
    })
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
            self.conn = task
                .await
                .map_err(|_| error(ErrorKind::Internal, "MySQL worker failed"))?;
            self.active = None;
        }
        self.conn.as_mut().ok_or_else(closed)
    }
}
#[async_trait]
impl Connection for MysqlConnection {
    fn cancellation_handle(&self) -> Arc<dyn CancelHandle> {
        self.next.clone()
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
        let cancel = std::mem::replace(&mut self.next, Cancellation::new());
        if cancel.cancelled.load(Ordering::Acquire) {
            return Err(error(ErrorKind::Cancelled, "MySQL query cancelled"));
        }
        self.connection().await?;
        let conn = self.conn.take().ok_or_else(closed)?;
        let (cursor, task) = runtime::start(
            conn,
            self.opts.clone(),
            runtime::Start {
                sql: sql.to_owned(),
                options,
                max,
                expected: None,
                cancel: cancel.clone(),
                independent: false,
                spool: spool::Spool::new()?,
                transport: self.transport.as_ref().ok_or_else(closed)?.clone(),
                tunnel: self._tunnel.clone(),
            },
        )
        .await?;
        self.pending = Some(task);
        self.active = Some(cancel);
        cursor.map(|c| Box::new(c) as Box<dyn ResultCursor>)
    }
    async fn inspect_edit_target(&mut self, object: &ObjectId) -> Result<EditTarget> {
        editing::inspect_target(self.connection().await?, object).await
    }
    async fn inspect_edit_query(
        &mut self,
        sql: &str,
        result_columns: Vec<String>,
    ) -> Result<EditQueryTarget> {
        editing::inspect_query(self.connection().await?, sql, result_columns).await
    }
    async fn apply_edit_batch(&mut self, batch: EditBatch) -> Result<EditBatchSummary> {
        editing::apply_batch(self.connection().await?, batch).await
    }
    async fn sql_mode(&mut self) -> Result<Option<String>> {
        self.connection()
            .await?
            .query_first("SELECT @@SESSION.sql_mode")
            .await
            .map_err(normalize)
    }
    async fn open_object(
        &mut self,
        object: &ObjectId,
        max: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        if self.conn.is_none() && self.pending.is_none() {
            return Err(closed());
        }
        let (db, table) = parse_table(object)?;
        let transport = reserve_transport()?;
        let mut conn = tokio::time::timeout(Duration::from_secs(15), Conn::new(self.opts.clone()))
            .await
            .map_err(|_| error(ErrorKind::Connection, "MySQL connection timed out"))?
            .map_err(normalize)?;
        let sql = format!("SELECT * FROM {}.{}", quote(&db), quote(&table));
        let stmt = tokio::time::timeout(Duration::from_secs(15), conn.prep(&sql))
            .await
            .map_err(|_| error(ErrorKind::Timeout, "MySQL object metadata timed out"))?
            .map_err(normalize)?;
        let schema = columns(stmt.columns(), max)?;
        conn.close(stmt).await.map_err(normalize)?;
        Ok(Box::new(ObjectCursor {
            conn: Some(conn),
            sql,
            columns: schema,
            max,
            cancel: Cancellation::new(),
            inner: None,
            opts: self.opts.clone(),
            task: None,
            _tunnel: self._tunnel.clone(),
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
    async fn load_metadata_page(
        &mut self,
        parent: Option<ObjectId>,
        offset: u64,
        page_size: u32,
    ) -> Result<MetadataPage> {
        if page_size == 0 || page_size > 10000 {
            return Err(error(ErrorKind::InvalidInput, "Invalid metadata page size"));
        }
        let mut objects = metadata(
            self.connection().await?,
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
        self.connection()
            .await?
            .query_drop("COMMIT")
            .await
            .map_err(normalize)
    }
    async fn rollback(&mut self) -> Result<()> {
        self.connection()
            .await?
            .query_drop("ROLLBACK")
            .await
            .map_err(normalize)
    }
    async fn close(&mut self) -> Result<()> {
        if let Some(cancel) = &self.active {
            cancel.cancel().await?;
        }
        let _ = self.connection().await;
        let result = if let Some(conn) = self.conn.take() {
            conn.disconnect().await.map_err(normalize)
        } else {
            Ok(())
        };
        self.transport.take();
        result
    }
}
struct ObjectCursor {
    conn: Option<Conn>,
    sql: String,
    columns: Vec<Column>,
    max: usize,
    cancel: Arc<Cancellation>,
    inner: Option<runtime::StreamCursor>,
    opts: Opts,
    task: Option<tokio::task::JoinHandle<Option<Conn>>>,
    spool: spool::Spool,
    _tunnel: Option<Arc<ssh::Tunnel>>,
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
    async fn fetch_page(&mut self, size: PageSize) -> Result<ResultPage> {
        self.fetch_page_bounded(size, 4 * 1024 * 1024).await
    }
    async fn fetch_page_bounded(&mut self, size: PageSize, max: usize) -> Result<ResultPage> {
        if self.cancel.cancelled.load(Ordering::Acquire) {
            return Err(error(ErrorKind::Cancelled, "MySQL object read cancelled"));
        }
        if self.inner.is_none() {
            let mut conn = self.conn.take().ok_or_else(closed)?;
            conn.query_drop("START TRANSACTION READ ONLY")
                .await
                .map_err(normalize)?;
            let (cursor, task) = runtime::start(
                conn,
                self.opts.clone(),
                runtime::Start {
                    sql: self.sql.clone(),
                    options: QueryOptions::default(),
                    max: self.max,
                    expected: Some(self.columns.clone()),
                    cancel: self.cancel.clone(),
                    independent: true,
                    spool: self.spool.clone(),
                    transport: self.transport.take().ok_or_else(closed)?,
                    tunnel: self._tunnel.clone(),
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
        if let Some(task) = self.task.take() {
            let _ = task.await;
        }
        self.conn.take();
        self.transport.take();
        Ok(())
    }
}
impl Drop for MysqlConnection {
    fn drop(&mut self) {
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
    size: PageSize,
    max: usize,
) -> Result<(ResultPage, u64, u64)> {
    let mut page = ResultPage {
        index,
        rows: Vec::new(),
        has_more: false,
    };
    if max < std::mem::size_of::<ResultPage>() {
        return Err(limit());
    }
    while position < count && page.rows.len() < size.get() as usize {
        let offset = file.stream_position().map_err(io_error)?;
        let mut bound = [0; 8];
        file.read_exact(&mut bound).map_err(io_error)?;
        let bound = u64::from_le_bytes(bound);
        // Bound includes JSON input, decoder scratch, row/value capacities and
        // row-vector growth, before either input or decoded values are allocated.
        if bound > max.saturating_sub(page.estimated_bytes()) as u64 {
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
        let row: Row = serde_json::from_slice(&bytes)
            .map_err(|_| error(ErrorKind::Internal, "Invalid MySQL result storage"))?;
        page.rows.reserve_exact(1);
        page.rows.push(row);
        position += 1;
    }
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
            for (name, kind) in rows {
                out.push(SchemaObject {
                    id: id(&[&parts[0], &name]),
                    parent: Some(parent.clone()),
                    qualified_name: format!("{}.{}", quote(&parts[0]), quote(&name)),
                    name,
                    kind: if kind == "VIEW" {
                        ObjectKind::View
                    } else {
                        ObjectKind::Table
                    },
                    has_children: true,
                    column: None,
                    properties: vec![],
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
}
