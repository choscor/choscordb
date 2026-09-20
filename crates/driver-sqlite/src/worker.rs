use super::*;
use rusqlite::{Connection as Db, OpenFlags, types::ValueRef};
use std::time::Instant;
pub(super) fn run(
    path: std::path::PathBuf,
    read_only: bool,
    mut rx: mpsc::Receiver<Command>,
    ready: oneshot::Sender<Result<Arc<Cancellation>>>,
) {
    let flags = if read_only {
        OpenFlags::SQLITE_OPEN_READ_ONLY
    } else {
        OpenFlags::SQLITE_OPEN_READ_WRITE | OpenFlags::SQLITE_OPEN_CREATE
    };
    let db = match Db::open_with_flags(path, flags) {
        Ok(db) => db,
        Err(e) => {
            let _ = ready.send(Err(normalize(e)));
            return;
        }
    };
    // SQLite opens a file before reading its schema. Force that read before
    // reporting the session as connected.
    if let Err(error) = db.query_row("PRAGMA schema_version", [], |row| row.get::<_, i64>(0)) {
        let error = normalize(error);
        drop(db);
        let _ = ready.send(Err(error));
        return;
    }
    let requested = Arc::new(AtomicBool::new(false));
    let cancel = Arc::new(Cancellation {
        generation: std::sync::Mutex::new(CancellationState::default()),
        interrupt: db.get_interrupt_handle(),
        requested: requested.clone(),
    });
    if ready.send(Ok(cancel.clone())).is_err() {
        return;
    }
    let mut pending = None;
    let mut generation = 0u32;
    loop {
        let Some(command) = pending.take().or_else(|| rx.blocking_recv()) else {
            break;
        };
        match command {
            Command::Execute(sql, options, max_schema_bytes, reply) => {
                let Some(next) = generation.checked_add(1) else {
                    let _ = reply.send(Err(DriverError::new(
                        ErrorKind::ResourceLimit,
                        "Cursor generation exhausted",
                    )));
                    continue;
                };
                generation = next;
                {
                    let Ok(mut g) = cancel.generation.lock() else {
                        break;
                    };
                    g.generation += 1;
                    requested.store(
                        g.cancelled_generation == Some(g.generation),
                        Ordering::Release,
                    );
                }
                let deadline = options.timeout.and_then(|t| Instant::now().checked_add(t));
                let stop = requested.clone();
                let _ = db.progress_handler(
                    1000,
                    Some(move || {
                        stop.load(Ordering::Acquire)
                            || deadline.is_some_and(|d| Instant::now() >= d)
                    }),
                );
                pending = execute(
                    &db,
                    &sql,
                    options,
                    generation,
                    &mut rx,
                    reply,
                    &requested,
                    deadline,
                    max_schema_bytes,
                );
                let _ = db.progress_handler(0, None::<fn() -> bool>);
            }
            Command::Object(operation) => operation(&db),
            Command::Metadata(parent, r) => {
                let _ = r.send(metadata::load(&db, parent));
            }
            Command::Ddl(id, r) => {
                let _ = r.send(metadata::ddl(&db, id));
            }
            Command::Edit(batch, r) => {
                let _ = r.send(apply_edit_batch(&db, batch));
            }
            Command::EditTarget(object, r) => {
                let _ = r.send(metadata::edit_target(&db, &object));
            }
            Command::EditQuery(sql, columns, r) => {
                let _ = r.send(metadata::edit_query(&db, &sql, columns));
            }
            Command::Transaction(commit, r) => {
                let result = if db.is_autocommit() {
                    Ok(())
                } else {
                    db.execute_batch(if commit { "COMMIT" } else { "ROLLBACK" })
                        .map_err(normalize)
                };
                let _ = r.send(result);
            }
            Command::Close(r) => {
                let _ = r.send(Ok(()));
                break;
            }
            Command::Fetch(_, _, _, r) => {
                let _ = r.send(Err(stale()));
            }
            Command::Load(_, _, r) => {
                let _ = r.send(Err(stale()));
            }
            Command::Finish(_, r) => {
                let _ = r.send(Ok(()));
            }
        }
    }
}
fn apply_edit_batch(db: &Db, batch: EditBatch) -> Result<EditBatchSummary> {
    use rusqlite::types::Value as SqlValue;
    if !db.is_autocommit() {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "Commit or roll back the active transaction before applying edits",
        ));
    }
    let mut affected_rows = Vec::with_capacity(batch.statements.len());
    db.execute_batch("BEGIN IMMEDIATE").map_err(normalize)?;
    let outcome: Result<()> = (|| {
        for edit in batch.statements {
            let values: Result<Vec<SqlValue>> = edit
                .params
                .into_iter()
                .map(|v| {
                    Ok(match v {
                        Value::Null => SqlValue::Null,
                        Value::Bool(v) => SqlValue::Integer(v as i64),
                        Value::Integer(v) => SqlValue::Integer(v),
                        Value::Real(v) => SqlValue::Real(v),
                        Value::Binary(v) => SqlValue::Blob(v),
                        Value::Decimal(v)
                        | Value::Text(v)
                        | Value::Date(v)
                        | Value::Time(v)
                        | Value::Timestamp(v)
                        | Value::Uuid(v)
                        | Value::Json(v) => SqlValue::Text(v),
                        Value::Deferred { .. } => {
                            return Err(DriverError::new(
                                ErrorKind::InvalidInput,
                                "Deferred values cannot be edited",
                            ));
                        }
                    })
                })
                .collect();
            let values = values?;
            let count = db
                .execute(&edit.sql, rusqlite::params_from_iter(values))
                .map_err(normalize)? as u64;
            if edit.expected_rows.is_some_and(|expected| expected != count) {
                return Err(DriverError::new(
                    ErrorKind::Query,
                    "Edit conflict: the original row has changed",
                ));
            }
            affected_rows.push(count);
        }
        Ok(())
    })();
    if let Err(error) = outcome {
        let _ = db.execute_batch("ROLLBACK");
        return Err(error);
    }
    db.execute_batch("COMMIT").map_err(normalize)?;
    Ok(EditBatchSummary { affected_rows })
}
fn stale() -> DriverError {
    DriverError::new(ErrorKind::StaleHandle, "Cursor is no longer active")
}
#[allow(clippy::too_many_arguments)]
fn execute(
    db: &Db,
    sql: &str,
    options: QueryOptions,
    id: u32,
    rx: &mut mpsc::Receiver<Command>,
    reply: Reply<Started>,
    cancel: &Arc<AtomicBool>,
    deadline: Option<Instant>,
    max_schema_bytes: usize,
) -> Option<Command> {
    let prep = (|| -> Result<_> {
        if cancel.load(Ordering::Acquire) {
            return Err(DriverError::new(
                ErrorKind::Cancelled,
                "Query cancelled before execution",
            ));
        }
        if !options.auto_commit && db.is_autocommit() {
            db.execute_batch("BEGIN").map_err(normalize)?;
        }
        let mut statement = db.prepare(sql).map_err(normalize)?;
        let schema_limit = || {
            DriverError::new(
                ErrorKind::ResourceLimit,
                "Result schema exceeds memory budget",
            )
        };
        // Bound even the temporary borrowed-column vector before asking rusqlite
        // to allocate it. No result names or type strings have been copied yet.
        let fixed = statement
            .column_count()
            .checked_mul(std::mem::size_of::<Column>())
            .and_then(|bytes| bytes.checked_add(std::mem::size_of::<Vec<Column>>()))
            .filter(|bytes| *bytes <= max_schema_bytes)
            .ok_or_else(schema_limit)?;
        let borrowed = statement.columns();
        borrowed.iter().try_fold(fixed, |bytes, column| {
            bytes
                .checked_add(column.name().len())
                .and_then(|bytes| bytes.checked_add(column.decl_type().unwrap_or("").len()))
                .filter(|bytes| *bytes <= max_schema_bytes)
                .ok_or_else(schema_limit)
        })?;
        let columns = borrowed
            .iter()
            .map(|c| Column {
                name: c.name().into(),
                database_type: c.decl_type().unwrap_or("").into(),
                precision: None,
                scale: None,
                timezone: None,
                nullable: None,
            })
            .collect::<Vec<_>>();
        drop(borrowed);
        // Allocate fallible result resources before any auto-committed write.
        let spool = spool::Spool::new(id)?;
        let affected = if columns.is_empty() {
            Some(statement.execute([]).map_err(normalize)? as u64)
        } else {
            None
        };
        Ok((statement, columns, affected, spool))
    })();
    let (mut statement, columns, affected, mut spool) = match prep {
        Ok(s) => s,
        Err(e) => {
            let mut e = e;
            if e.kind == ErrorKind::Cancelled && deadline.is_some_and(|d| Instant::now() >= d) {
                e.kind = ErrorKind::Timeout;
            }
            let _ = reply.send(Err(e));
            return None;
        }
    };
    let width = columns.len();
    let _ = reply.send(Ok(Started {
        reader: spool.reader(),
        id,
        columns,
        summary: QuerySummary {
            transaction_active: Some(!db.is_autocommit()),
            affected_rows: affected,
            warnings: vec![],
            has_more_results: false,
            sql_mode: None,
        },
    }));
    let mut rows = match statement.query([]) {
        Ok(r) => r,
        Err(_) => return None,
    };
    let mut index = 0;
    let mut exhausted = width == 0;
    let mut stepped = false;
    let mut buffered = None;
    while let Some(command) = rx.blocking_recv() {
        match command {
            Command::Fetch(cursor, size, max_bytes, r) if cursor == id => {
                let page = (|| -> Result<ResultPage> {
                    if cancel.load(Ordering::Acquire) {
                        return Err(DriverError::new(ErrorKind::Cancelled, "Query cancelled"));
                    }
                    if deadline.is_some_and(|d| Instant::now() >= d) {
                        return Err(DriverError::new(ErrorKind::Timeout, "Query timed out"));
                    }
                    // Reserve fixed container storage first. Each column may need a
                    // four-byte deferred type tag, even when its source is huge.
                    let minimum_row = width
                        .checked_mul(std::mem::size_of::<Value>() + 4)
                        .ok_or_else(memory_limit)?;
                    let available = max_bytes
                        .checked_sub(std::mem::size_of::<ResultPage>())
                        .ok_or_else(memory_limit)?;
                    let capacity = (available / (std::mem::size_of::<Row>() + minimum_row))
                        .min(size.get() as usize);
                    if capacity == 0 {
                        return Err(memory_limit());
                    }
                    let mut page = Vec::with_capacity(capacity);
                    let mut bytes = std::mem::size_of::<ResultPage>()
                        + page.capacity() * std::mem::size_of::<Row>();
                    let row_budget = max_bytes - bytes;
                    if buffered
                        .as_ref()
                        .is_some_and(|row: &Row| row_bytes(row) > row_budget)
                    {
                        return Err(memory_limit());
                    }
                    while page.len() < capacity && !exhausted {
                        let row = if let Some(row) = buffered.take() {
                            Some(row)
                        } else {
                            stepped = true;
                            next_row(&mut rows, width, &mut spool, row_budget)?
                        };
                        match row {
                            Some(row) => {
                                let allocation = row_bytes(&row);
                                if allocation > max_bytes - bytes {
                                    buffered = Some(row);
                                    break;
                                }
                                bytes += allocation;
                                page.push(row);
                            }
                            None => exhausted = true,
                        }
                    }
                    if !exhausted && buffered.is_none() {
                        stepped = true;
                        buffered = next_row(&mut rows, width, &mut spool, row_budget)?;
                        exhausted = buffered.is_none();
                    }
                    page.shrink_to_fit();
                    let result = ResultPage {
                        index,
                        rows: page,
                        has_more: !exhausted,
                    };
                    index += 1;
                    Ok(result)
                })()
                .map_err(|mut e| {
                    if e.kind == ErrorKind::Cancelled
                        && deadline.is_some_and(|d| Instant::now() >= d)
                    {
                        e.kind = ErrorKind::Timeout;
                    }
                    e
                });
                let _ = r.send(page);
            }
            Command::Object(operation) => {
                let _ = db.progress_handler(0, None::<fn() -> bool>);
                operation(db);
                let stop = cancel.clone();
                let _ = db.progress_handler(
                    1000,
                    Some(move || {
                        stop.load(Ordering::Acquire)
                            || deadline.is_some_and(|d| Instant::now() >= d)
                    }),
                );
            }
            Command::Metadata(parent, reply) => {
                // Metadata is a separate operation on the same connection. Its SQL
                // must not inherit an exhausted result's query deadline.
                let _ = db.progress_handler(0, None::<fn() -> bool>);
                let _ = reply.send(metadata::load(db, parent));
                let stop = cancel.clone();
                let _ = db.progress_handler(
                    1000,
                    Some(move || {
                        stop.load(Ordering::Acquire)
                            || deadline.is_some_and(|d| Instant::now() >= d)
                    }),
                );
            }
            Command::Ddl(object, reply) => {
                let _ = db.progress_handler(0, None::<fn() -> bool>);
                let _ = reply.send(metadata::ddl(db, object));
                let stop = cancel.clone();
                let _ = db.progress_handler(
                    1000,
                    Some(move || {
                        stop.load(Ordering::Acquire)
                            || deadline.is_some_and(|d| Instant::now() >= d)
                    }),
                );
            }
            Command::EditTarget(object, reply) => {
                let _ = db.progress_handler(0, None::<fn() -> bool>);
                let _ = reply.send(metadata::edit_target(db, &object));
                let stop = cancel.clone();
                let _ = db.progress_handler(
                    1000,
                    Some(move || {
                        stop.load(Ordering::Acquire)
                            || deadline.is_some_and(|d| Instant::now() >= d)
                    }),
                );
            }
            Command::EditQuery(sql, columns, reply) => {
                let _ = db.progress_handler(0, None::<fn() -> bool>);
                let _ = reply.send(metadata::edit_query(db, &sql, columns));
                let stop = cancel.clone();
                let _ = db.progress_handler(
                    1000,
                    Some(move || {
                        stop.load(Ordering::Acquire)
                            || deadline.is_some_and(|d| Instant::now() >= d)
                    }),
                );
            }
            Command::Load(cursor, handle, r) if cursor == id => {
                let _ = r.send(spool.load(handle));
            }
            Command::Close(reply) => {
                if stepped && !exhausted {
                    abort_suspended_rows(db, &mut rows);
                }
                return Some(Command::Close(reply));
            }
            Command::Finish(cursor, r) if cursor == id => {
                if stepped
                    && !exhausted
                    && (cancel.load(Ordering::Acquire)
                        || deadline.is_some_and(|d| Instant::now() >= d))
                {
                    abort_suspended_rows(db, &mut rows);
                }
                let _ = r.send(Ok(()));
                return None;
            }
            Command::Finish(_, r) => {
                let _ = r.send(Ok(()));
            }
            Command::Fetch(_, _, _, r) => {
                let _ = r.send(Err(stale()));
            }
            Command::Load(_, _, r) => {
                let _ = r.send(Err(stale()));
            }
            other => return Some(other),
        }
    }
    None
}
fn abort_suspended_rows(db: &Db, rows: &mut rusqlite::Rows<'_>) {
    // Finalizing a suspended automatic RETURNING statement commits its write.
    // Resume the interrupted existing VM so SQLite rolls back before finalization.
    // Callers exclude unstarted/exhausted results and already-executed DDL.
    let _ = db.progress_handler(1, Some(|| true));
    db.get_interrupt_handle().interrupt();
    let _ = rows.next();
}
pub(super) fn next_row(
    rows: &mut rusqlite::Rows<'_>,
    width: usize,
    spool: &mut spool::Spool,
    max_bytes: usize,
) -> Result<Option<Row>> {
    let Some(row) = rows.next().map_err(normalize)? else {
        return Ok(None);
    };
    let mut values = Vec::with_capacity(width);
    // Split payload allowance evenly so later columns always fit, without
    // copying a source TEXT/BLOB before deciding whether it must be deferred.
    let payload_limit = ((max_bytes - values.capacity() * std::mem::size_of::<Value>())
        / width.max(1))
    .min(16 * 1024);
    for i in 0..width {
        let value = match row.get_ref(i).map_err(normalize)? {
            ValueRef::Null => Value::Null,
            ValueRef::Integer(n) => Value::Integer(n),
            ValueRef::Real(n) => Value::Real(n),
            ValueRef::Text(b) if b.len() > payload_limit => spool.store(b, true)?,
            ValueRef::Blob(b) if b.len() > payload_limit => spool.store(b, false)?,
            ValueRef::Text(b) => Value::Text(
                std::str::from_utf8(b)
                    .map_err(|_| {
                        DriverError::new(ErrorKind::Query, "Invalid UTF-8 in SQLite text")
                    })?
                    .into(),
            ),
            ValueRef::Blob(b) => Value::Binary(b.to_vec()),
        };
        values.push(value);
    }
    Ok(Some(values))
}

fn memory_limit() -> DriverError {
    DriverError::new(
        ErrorKind::ResourceLimit,
        "Result page exceeds memory budget",
    )
}
pub(super) fn row_bytes(row: &Row) -> usize {
    row.capacity() * std::mem::size_of::<Value>()
        + row
            .iter()
            .map(|value| value.estimated_bytes() - std::mem::size_of::<Value>())
            .sum::<usize>()
}
