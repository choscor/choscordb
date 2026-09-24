//! Incremental, disk-backed results. Only the worker owns the wire protocol.
use super::*;
use std::sync::Mutex;

// The wire codec may retain a 64 MiB packet and decoded row simultaneously.
// Admit at most two workers to this native working-memory pool, independently
// of the core's bounded page/schema reservations.
static CONTROL_TRANSPORTS: tokio::sync::Semaphore = tokio::sync::Semaphore::const_new(2);
static NATIVE_WORKERS: tokio::sync::Semaphore = tokio::sync::Semaphore::const_new(2);
struct Waiting(Option<Arc<Cancellation>>);
impl Drop for Waiting {
    fn drop(&mut self) {
        if let Some(cancel) = &self.0 {
            cancel.cancelled.store(true, Ordering::Release);
            cancel.changed.notify_waiters();
        }
    }
}
async fn admit(cancel: &Arc<Cancellation>) -> Result<tokio::sync::SemaphorePermit<'static>> {
    let changed = cancel.changed.notified();
    tokio::pin!(changed);
    changed.as_mut().enable();
    if cancel.cancelled.load(Ordering::Acquire) {
        return Err(error(ErrorKind::Cancelled, "MySQL query cancelled").with_code("1317"));
    }
    tokio::select! { biased;
        _ = changed => Err(error(ErrorKind::Cancelled, "MySQL query cancelled").with_code("1317")),
        permit = NATIVE_WORKERS.acquire() => permit.map_err(|_| error(ErrorKind::Internal, "MySQL memory admission closed")),
    }
}

struct Set {
    file: Arc<Mutex<std::fs::File>>,
    columns: Vec<Column>,
    count: u64,
    done: bool,
    finalized: bool,
    summary: QuerySummary,
}
struct State {
    sets: Vec<Set>,
    done: bool,
    failure: Option<(usize, DriverError)>,
}
struct Shared {
    state: Mutex<State>,
    changed: tokio::sync::Notify,
    spool: spool::Spool,
    idle: idle::Tracker,
    native: Mutex<Option<Arc<tokio::sync::SemaphorePermit<'static>>>>,
}
impl Shared {
    fn lock(&self) -> Result<std::sync::MutexGuard<'_, State>> {
        self.state
            .lock()
            .map_err(|_| error(ErrorKind::Internal, "MySQL result lock failed"))
    }
}
pub(super) struct StreamCursor {
    shared: Arc<Shared>,
    columns: Vec<Column>,
    set: usize,
    position: u64,
    offset: u64,
    page: u64,
    cancel: Arc<Cancellation>,
    independent: bool,
    closed: bool,
}

pub(super) struct Start {
    pub idle: idle::Tracker,
    pub sql: String,
    pub options: QueryOptions,
    pub max: usize,
    pub expected: Option<Vec<Column>>,
    pub cancel: Arc<Cancellation>,
    pub independent: bool,
    pub spool: spool::Spool,
    pub transport: Arc<tokio::sync::OwnedSemaphorePermit>,
    pub tunnel: Option<Arc<ssh::Tunnel>>,
    pub proxy: Option<Arc<crate::proxy::Tunnel>>,
}
pub(super) async fn start(
    conn: Conn,
    opts: Opts,
    request: Start,
) -> Result<(Result<StreamCursor>, tokio::task::JoinHandle<Option<Conn>>)> {
    let Start {
        idle,
        sql,
        options,
        max,
        expected,
        cancel,
        independent,
        spool,
        transport,
        tunnel,
        proxy,
    } = request;
    let shared = Arc::new(Shared {
        state: Mutex::new(State {
            sets: Vec::new(),
            done: false,
            failure: None,
        }),
        changed: tokio::sync::Notify::new(),
        spool,
        idle,
        native: Mutex::new(None),
    });
    let worker_shared = shared.clone();
    let worker_cancel = cancel.clone();
    let conn = connection::PendingConnection::new(conn);
    let task = tokio::spawn(async move {
        let _transport = transport;
        let _tunnel = tunnel;
        let _proxy = proxy;
        let mut conn = conn;
        let id = conn.connection().id();
        let timeout = options.timeout;
        let result = {
            let notified = worker_cancel.changed.notified();
            tokio::pin!(notified);
            notified.as_mut().enable();
            let operation = async {
                let permit = Arc::new(admit(&worker_cancel).await?);
                *worker_shared
                    .native
                    .lock()
                    .map_err(|_| error(ErrorKind::Internal, "MySQL admission lock failed"))? =
                    Some(permit);
                let result = produce(
                    conn.connection(),
                    &sql,
                    options,
                    max,
                    expected,
                    &worker_shared,
                    &worker_cancel,
                )
                .await;
                if result
                    .as_ref()
                    .err()
                    .is_some_and(|error| error.vendor_code.is_some())
                {
                    // An ERR packet clears mysql_async's last OK status even when
                    // the transaction remains active. Refresh server state before
                    // reusing the session; guessing inactive could commit prior work
                    // when the next manual query issues START TRANSACTION.
                    if !matches!(
                        tokio::time::timeout(Duration::from_secs(5), conn.connection().ping())
                            .await,
                        Ok(Ok(()))
                    ) {
                        return Err(error(
                            ErrorKind::Disconnected,
                            "MySQL transaction state recovery failed; connection closed",
                        ));
                    }
                }
                worker_shared.idle.reconcile(conn.connection());
                result
            };
            tokio::pin!(operation);
            let deadline = async {
                match timeout {
                    Some(duration) => tokio::time::sleep(duration).await,
                    None => std::future::pending().await,
                }
            };
            let interrupted = if worker_cancel.cancelled.load(Ordering::Acquire) {
                Err(ErrorKind::Cancelled)
            } else {
                tokio::select! { biased;
                    _ = &mut notified => Err(ErrorKind::Cancelled),
                    _ = deadline => Err(ErrorKind::Timeout),
                    result = &mut operation => Ok(result),
                }
            };
            match interrupted {
                Ok(result) => result,
                Err(kind) => {
                    // Draining may yield successful rows (notably interrupted
                    // SLEEP). Publish the interruption before polling that drain,
                    // so readers cannot mistake those rows for query success.
                    if let Ok(mut state) = worker_shared.lock() {
                        let set = state.sets.len().saturating_sub(1);
                        state.failure = Some((
                            set,
                            error(kind, "MySQL query interrupted").with_code("1317"),
                        ));
                    }
                    worker_cancel.cancelled.store(true, Ordering::Release);
                    worker_shared.changed.notify_waiters();
                    // Keep the original future alive: dropping an in-flight read can
                    // desynchronize packet framing. Kill from a second transport,
                    // then drain the original response before returning the session.
                    let cleanup = async {
                        let _control = CONTROL_TRANSPORTS.acquire().await.map_err(|_| {
                            error(ErrorKind::Internal, "MySQL control admission closed")
                        })?;
                        let opts =
                            OptsBuilder::from_opts(opts).max_allowed_packet(Some(1024 * 1024));
                        let mut control = connection::connect(opts.into()).await?;
                        control
                            .connection()
                            .query_drop(format!("KILL QUERY {id}"))
                            .await
                            .map_err(normalize)?;
                        drop(control);
                        let drained = operation.await;
                        match drained {
                            Ok(()) => Ok(()),
                            Err(e) if e.vendor_code.is_some() => Ok(()),
                            Err(e) => Err(e),
                        }
                    };
                    if matches!(
                        tokio::time::timeout(Duration::from_secs(5), cleanup).await,
                        Ok(Ok(()))
                    ) {
                        Err(error(kind, "MySQL query interrupted").with_code("1317"))
                    } else {
                        Err(error(kind, "MySQL query interrupted; connection closed"))
                    }
                }
            }
        };
        if let Ok(mut native) = worker_shared.native.lock() {
            native.take();
        }
        let reusable = result
            .as_ref()
            .err()
            .is_none_or(|e| e.vendor_code.is_some());
        if let Ok(mut state) = worker_shared.lock() {
            state.done = true;
            let interrupted = result
                .as_ref()
                .err()
                .is_some_and(|e| matches!(e.kind, ErrorKind::Cancelled | ErrorKind::Timeout));
            let failed_set = if interrupted {
                state.sets.len().saturating_sub(1)
            } else {
                state
                    .sets
                    .last()
                    .map_or(0, |set| state.sets.len() - usize::from(!set.done))
            };
            state.failure = result.err().map(|failure| (failed_set, failure));
            if state.failure.is_some() && failed_set > 0 {
                state.sets[failed_set - 1].summary.has_more_results = true;
            }
            if independent {
                for set in &mut state.sets {
                    set.summary.transaction_active = None;
                    set.summary.sql_mode = None;
                }
            }
        }
        worker_shared.changed.notify_waiters();
        if independent || !reusable {
            drop(conn);
            None
        } else {
            Some(conn.ready())
        }
    });
    let mut waiting = Waiting(Some(cancel.clone()));
    // Do not wait for row spooling. Non-row commands wait for completion so
    // affected-row and transaction summaries remain authoritative on return.
    loop {
        let changed = shared.changed.notified();
        tokio::pin!(changed);
        changed.as_mut().enable();
        let ready = {
            let state = shared.lock()?;
            if let Some((0, failure)) = &state.failure {
                waiting.0 = None;
                return Ok((Err(failure.clone()), task));
            }
            state
                .sets
                .first()
                .filter(|set| !set.columns.is_empty() || set.finalized || state.done)
                .map(|set| set.columns.clone())
        };
        if let Some(columns) = ready {
            waiting.0 = None;
            return Ok((
                Ok(StreamCursor {
                    shared: shared.clone(),
                    columns,
                    set: 0,
                    position: 0,
                    offset: 0,
                    page: 0,
                    cancel,
                    independent,
                    closed: false,
                }),
                task,
            ));
        }
        changed.await;
    }
}

async fn produce(
    conn: &mut Conn,
    sql: &str,
    options: QueryOptions,
    max: usize,
    expected: Option<Vec<Column>>,
    shared: &Arc<Shared>,
    cancel: &Arc<Cancellation>,
) -> Result<()> {
    if max < std::mem::size_of::<Vec<Column>>() {
        return Err(limit());
    }
    // Preparing the complete input first preserves compound routine bodies. It
    // also rejects multiple statements before any side effects occur.
    // Preparing metadata can replace status flags without changing the session's
    // transaction. Capture the last completed SQL state before preparing.
    let transaction_started = transaction_active(conn).unwrap_or(false);
    let whole = conn.prep(sql).await;
    match whole {
        Ok(stmt) => {
            produce_statement(
                conn,
                sql,
                Some(stmt),
                &options,
                max,
                (expected, false, cancel, transaction_started),
                shared,
            )
            .await?;
        }
        Err(mysql_async::Error::Server(ref e)) if e.code == 1064 => {
            let mut remaining = sql;
            while !remaining.trim().is_empty() {
                if cancel.cancelled.load(Ordering::Acquire) {
                    return Err(
                        error(ErrorKind::Cancelled, "MySQL script interrupted").with_code("1317")
                    );
                }
                let mode: Option<String> = conn
                    .query_first("SELECT @@SESSION.sql_mode")
                    .await
                    .map_err(normalize)?;
                let mode = choscordb_sql_language::MysqlSqlMode::from_sql_mode(
                    mode.as_deref().unwrap_or(""),
                );
                let ranges =
                    choscordb_sql_language::statement_ranges_mysql_with_mode(remaining, mode);
                let Some(range) = ranges.first() else {
                    break;
                };
                let statement = &remaining[range.clone()];
                let transaction_started = transaction_active(conn).unwrap_or(false);
                let stmt = match conn.prep(statement).await {
                    Ok(stmt) => Some(stmt),
                    Err(mysql_async::Error::Server(e)) if e.code == 1295 => None,
                    Err(e) => return Err(normalize(e)),
                };
                if cancel.cancelled.load(Ordering::Acquire) {
                    return Err(
                        error(ErrorKind::Cancelled, "MySQL script interrupted").with_code("1317")
                    );
                }
                let more = !remaining[range.end..].trim().is_empty();
                produce_statement(
                    conn,
                    statement,
                    stmt,
                    &options,
                    max,
                    (None, more, cancel, transaction_started),
                    shared,
                )
                .await?;
                remaining = &remaining[range.end..];
                if !remaining.trim().is_empty() {
                    if let Some(last) = shared.lock()?.sets.last_mut() {
                        last.summary.has_more_results = true;
                    }
                    shared.changed.notify_waiters();
                }
            }
        }
        Err(mysql_async::Error::Server(e)) if e.code == 1295 => {
            let mode: Option<String> = conn
                .query_first("SELECT @@SESSION.sql_mode")
                .await
                .map_err(normalize)?;
            let ranges = choscordb_sql_language::statement_ranges_mysql_with_mode(
                sql,
                choscordb_sql_language::MysqlSqlMode::from_sql_mode(mode.as_deref().unwrap_or("")),
            );
            if ranges.len() != 1 {
                return Err(error(
                    ErrorKind::Unsupported,
                    "Compound routine definitions and ambiguous administrative scripts require an external MySQL client; CALL results are supported",
                ));
            }
            let transaction_started = transaction_active(conn).unwrap_or(false);
            produce_statement(
                conn,
                sql,
                None,
                &options,
                max,
                (expected, false, cancel, transaction_started),
                shared,
            )
            .await?;
        }
        Err(e) => return Err(normalize(e)),
    }
    Ok(())
}
async fn produce_statement(
    conn: &mut Conn,
    sql: &str,
    stmt: Option<mysql_async::Statement>,
    options: &QueryOptions,
    max: usize,
    shape: (Option<Vec<Column>>, bool, &Arc<Cancellation>, bool),
    shared: &Arc<Shared>,
) -> Result<()> {
    let (expected, more_statements, cancel, transaction_started) = shape;
    if let Some(stmt) = &stmt {
        let schema = columns(stmt.columns(), max)?;
        if expected
            .as_ref()
            .is_some_and(|expected| expected != &schema)
        {
            return Err(error(
                ErrorKind::Query,
                "MySQL object schema changed; reopen the object",
            ));
        }
    }
    let started = std::time::Instant::now();
    if !options.auto_commit && !transaction_started {
        conn.query_drop("START TRANSACTION")
            .await
            .map_err(normalize)?;
        shared.idle.observe(conn, "START TRANSACTION", started);
    }
    if cancel.cancelled.load(Ordering::Acquire) {
        return Err(error(ErrorKind::Cancelled, "MySQL query interrupted").with_code("1317"));
    }
    let first = shared.lock()?.sets.len();
    if let Some(stmt) = stmt {
        let result = conn.exec_iter(&stmt, ()).await.map_err(normalize)?;
        collect(result, max, shared).await?;
        conn.close(stmt).await.map_err(normalize)?;
    } else {
        // Administrative SQL unsupported by the prepared protocol is one server
        // statement, validated by the mode-aware splitter above.
        let result = conn.query_iter(sql).await.map_err(normalize)?;
        collect(result, max, shared).await?;
    }
    shared.idle.observe(conn, sql, started);
    let transaction_active = transaction_active(conn);
    let sql_mode: Option<String> = conn
        .query_first("SELECT @@SESSION.sql_mode")
        .await
        .map_err(normalize)?;
    let mut state = shared.lock()?;
    if more_statements && let Some(last) = state.sets.last_mut() {
        last.summary.has_more_results = true;
    }
    for set in &mut state.sets[first..] {
        set.finalized = true;
        set.summary.transaction_active = transaction_active;
        set.summary.sql_mode = sql_mode.clone();
    }
    drop(state);
    shared.changed.notify_waiters();
    Ok(())
}
async fn collect<P: mysql_async::prelude::Protocol>(
    mut result: mysql_async::QueryResult<'_, '_, P>,
    max: usize,
    shared: &Arc<Shared>,
) -> Result<()> {
    loop {
        let wire_columns = result.columns().unwrap_or_else(|| Arc::from([]));
        let schema = columns(&wire_columns, max)?;
        let file = Arc::new(Mutex::new(tempfile::tempfile().map_err(io_error)?));
        let index = {
            let mut state = shared.lock()?;
            let index = state.sets.len();
            state.sets.push(Set {
                file: file.clone(),
                columns: schema.clone(),
                count: 0,
                done: false,
                finalized: false,
                summary: QuerySummary::default(),
            });
            index
        };
        shared.changed.notify_waiters();
        while let Some(row) = result.next().await.map_err(normalize)? {
            let wire_columns = wire_columns.clone();
            let file = file.clone();
            let mut spool = shared.spool.clone();
            // Blocking disk work cannot be aborted. Keep its admission alive if
            // cancellation drops the async producer before the write finishes.
            let native = shared
                .native
                .lock()
                .map_err(|_| error(ErrorKind::Internal, "MySQL admission lock failed"))?
                .clone();
            tokio::task::spawn_blocking(move || -> Result<()> {
                let _native = native;
                let mut row: Row = row
                    .unwrap()
                    .into_iter()
                    .zip(wire_columns.iter())
                    .map(|(v, c)| value(v, c))
                    .collect::<Result<_>>()?;
                let mut inline_bytes = 0usize;
                for value in &mut row {
                    let deferred = match value {
                        Value::Text(text) | Value::Json(text)
                            if text.len() > 16384
                                || inline_bytes.saturating_add(text.len()) > 65536 =>
                        {
                            Some(spool.store(text.as_bytes(), true)?)
                        }
                        Value::Binary(bytes)
                            if bytes.len() > 16384
                                || inline_bytes.saturating_add(bytes.len()) > 65536 =>
                        {
                            Some(spool.store(bytes, false)?)
                        }
                        _ => None,
                    };
                    if let Some(deferred) = deferred {
                        *value = deferred;
                    }
                    inline_bytes = inline_bytes.saturating_add(value.estimated_bytes());
                }
                // Reject pathological scalar/column widths before JSON allocates.
                // Six bytes per input byte covers worst-case JSON escaping.
                let serialization_bound = row
                    .iter()
                    .try_fold(0usize, |total, value| {
                        total.checked_add(
                            value
                                .estimated_bytes()
                                .saturating_mul(6)
                                .saturating_add(128),
                        )
                    })
                    .ok_or_else(limit)?;
                if serialization_bound > 4 * 1024 * 1024 {
                    return Err(limit());
                }
                let bytes = serde_json::to_vec(&row)
                    .map_err(|_| error(ErrorKind::Query, "Cannot encode MySQL result"))?;
                let bound = bytes
                    .len()
                    .saturating_mul(4)
                    .saturating_add(
                        row.len().max(4).next_power_of_two() * std::mem::size_of::<Value>() * 2,
                    )
                    .saturating_add(std::mem::size_of::<Row>());
                let mut file = file
                    .lock()
                    .map_err(|_| error(ErrorKind::Internal, "MySQL result lock failed"))?;
                file.seek(SeekFrom::End(0)).map_err(io_error)?;
                file.write_all(&(bound as u64).to_le_bytes())
                    .map_err(io_error)?;
                file.write_all(&(bytes.len() as u64).to_le_bytes())
                    .map_err(io_error)?;
                file.write_all(&bytes).map_err(io_error)?;
                Ok(())
            })
            .await
            .map_err(|_| error(ErrorKind::Internal, "MySQL result worker failed"))??;
            shared.lock()?.sets[index].count += 1;
            shared.changed.notify_waiters();
        }
        let more = result.columns().is_some();
        {
            let mut state = shared.lock()?;
            let set = &mut state.sets[index];
            set.done = true;
            set.summary.affected_rows = schema.is_empty().then(|| result.affected_rows());
            set.summary.has_more_results = more;
        }
        shared.changed.notify_waiters();
        if !more {
            break;
        }
    }
    result.drop_result().await.map_err(normalize)?;
    Ok(())
}
impl StreamCursor {
    fn check_interruption(&self, state: &State, set: usize) -> Result<()> {
        if let Some((index, failure)) = &state.failure
            && (set >= *index || matches!(failure.kind, ErrorKind::Cancelled | ErrorKind::Timeout))
        {
            return Err(failure.clone());
        }
        if !state.done && self.cancel.cancelled.load(Ordering::Acquire) {
            return Err(error(ErrorKind::Cancelled, "MySQL query cancelled").with_code("1317"));
        }
        Ok(())
    }
}
#[async_trait]
impl ResultCursor for StreamCursor {
    fn independent_cancellation_handle(&self) -> Option<Arc<dyn CancelHandle>> {
        Some(self.cancel.clone())
    }
    fn deferred_reader(&self) -> Option<Arc<dyn DeferredReader>> {
        Some(self.shared.spool.reader())
    }
    fn columns(&self) -> &[Column] {
        &self.columns
    }
    async fn fetch_page(&mut self, size: PageSize) -> Result<ResultPage> {
        self.fetch_page_bounded(size, 4 * 1024 * 1024).await
    }
    async fn fetch_page_bounded(&mut self, size: PageSize, max: usize) -> Result<ResultPage> {
        if self.closed {
            return Err(closed());
        }
        loop {
            let changed = self.shared.changed.notified();
            tokio::pin!(changed);
            changed.as_mut().enable();
            let available = {
                let state = self.shared.lock()?;
                self.check_interruption(&state, self.set)?;
                let set = &state.sets[self.set];
                if set.count > self.position.saturating_add(u64::from(size.get()))
                    || set.done && (set.finalized || state.done || self.set + 1 < state.sets.len())
                {
                    Some((set.file.clone(), set.count, set.done))
                } else {
                    None
                }
            };
            if let Some((file, count, done)) = available {
                let position = self.position;
                let offset = self.offset;
                let index = self.page;
                let (mut page, position, offset) = tokio::task::spawn_blocking(move || {
                    let mut file = file
                        .lock()
                        .map_err(|_| error(ErrorKind::Internal, "MySQL result lock failed"))?;
                    file.seek(SeekFrom::Start(offset)).map_err(io_error)?;
                    read_page(&mut file, position, count, index, size, max)
                })
                .await
                .map_err(|_| error(ErrorKind::Internal, "MySQL result worker failed"))??;
                // Disk work yields to cancellation and producer failures. Do not
                // expose the page or advance its position after either occurred.
                {
                    let state = self.shared.lock()?;
                    self.check_interruption(&state, self.set)?;
                }
                page.has_more |= !done;
                self.position = position;
                self.offset = offset;
                self.page += 1;
                return Ok(page);
            }
            changed.await;
        }
    }
    async fn next_result_set(&mut self) -> Result<bool> {
        loop {
            let changed = self.shared.changed.notified();
            tokio::pin!(changed);
            changed.as_mut().enable();
            {
                let state = self.shared.lock()?;
                self.check_interruption(&state, self.set + 1)?;
                if let Some(next) = state.sets.get(self.set + 1) {
                    self.set += 1;
                    self.columns = next.columns.clone();
                    self.position = 0;
                    self.offset = 0;
                    self.page = 0;
                    return Ok(true);
                }
                if state.done {
                    return Ok(false);
                }
            }
            changed.await;
        }
    }
    fn summary(&self) -> QuerySummary {
        self.shared
            .lock()
            .map(|state| {
                let mut summary = state.sets[self.set].summary.clone();
                if self.independent {
                    summary.transaction_active = None;
                    summary.sql_mode = None;
                }
                summary
            })
            .unwrap_or_default()
    }
    async fn close(&mut self) -> Result<()> {
        self.closed = true;
        if !self.shared.lock()?.done {
            self.cancel.cancel().await?;
        }
        Ok(())
    }
}
impl Drop for StreamCursor {
    fn drop(&mut self) {
        if self.shared.lock().is_ok_and(|state| !state.done) {
            self.cancel.cancelled.store(true, Ordering::Release);
            self.cancel.changed.notify_waiters();
        }
    }
}
