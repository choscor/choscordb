use super::*;
use futures_util::TryStreamExt;
use std::sync::atomic::Ordering;
use tokio_postgres::{Portal, Transaction};
struct Active {
    deadline: Option<std::time::Instant>,
    id: u32,
    portal: Portal,
    spool: Option<spool::Spool>,
    index: u64,
    summary: QuerySummary,
    completed: bool,
    lookahead: Option<Row>,
}
fn stale() -> DriverError {
    DriverError::new(
        ErrorKind::StaleHandle,
        "PostgreSQL result is no longer active",
    )
}
fn resource() -> DriverError {
    DriverError::new(
        ErrorKind::ResourceLimit,
        "PostgreSQL result exceeds memory budget",
    )
}
fn requires_implicit_transaction(sql: &str) -> bool {
    let bytes = sql.as_bytes();
    let mut offset = 0;
    let mut words = Vec::with_capacity(2);
    while offset < bytes.len() && words.len() < 2 {
        if bytes[offset].is_ascii_whitespace() {
            offset += 1;
        } else if bytes[offset..].starts_with(b"--") {
            offset += 2;
            while offset < bytes.len() && bytes[offset] != b'\n' {
                offset += 1;
            }
        } else if bytes[offset..].starts_with(b"/*") {
            offset += 2;
            let mut depth = 1usize;
            while offset < bytes.len() && depth > 0 {
                if bytes[offset..].starts_with(b"/*") {
                    depth += 1;
                    offset += 2;
                } else if bytes[offset..].starts_with(b"*/") {
                    depth -= 1;
                    offset += 2;
                } else {
                    offset += 1;
                }
            }
            if depth != 0 {
                return false;
            }
        } else if bytes[offset].is_ascii_alphabetic() {
            let start = offset;
            while offset < bytes.len()
                && (bytes[offset].is_ascii_alphanumeric() || bytes[offset] == b'_')
            {
                offset += 1;
            }
            words.push(sql[start..offset].to_ascii_uppercase());
        } else {
            break;
        }
    }
    match words.as_slice() {
        [first, ..]
            if matches!(
                first.as_str(),
                "VACUUM" | "CHECKPOINT" | "REINDEX" | "CLUSTER"
            ) =>
        {
            true
        }
        [first, second]
            if matches!(
                (first.as_str(), second.as_str()),
                ("CREATE", "DATABASE" | "TABLESPACE")
                    | ("DROP", "DATABASE" | "TABLESPACE")
                    | ("CREATE", "INDEX")
                    | ("DROP", "INDEX")
                    | ("ALTER", "SYSTEM")
                    | ("DISCARD", "ALL")
                    | ("REFRESH", "MATERIALIZED")
            ) =>
        {
            true
        }
        _ => false,
    }
}
async fn begin_generation(cancel: &Cancellation) -> Result<u32> {
    let mut state = cancel.state.lock().await;
    state.current = state.current.checked_add(1).ok_or_else(resource)?;
    cancel
        .next
        .store(state.current.saturating_add(1), Ordering::Release);
    state.running = false;
    if state.cancelled == Some(state.current) {
        return Err(DriverError::new(ErrorKind::Cancelled, "Query cancelled"));
    }
    u32::try_from(state.current).map_err(|_| resource())
}
async fn running(cancel: &Cancellation, value: bool) -> Result<()> {
    let mut state = cancel.state.lock().await;
    state.running = value;
    if value && state.cancelled == Some(state.current) {
        state.running = false;
        return Err(DriverError::new(ErrorKind::Cancelled, "Query cancelled"));
    }
    Ok(())
}
fn clear_notices(notices: &Mutex<Vec<String>>) {
    if let Ok(mut notices) = notices.lock() {
        notices.clear();
    }
}
fn collect_notices(summary: &mut QuerySummary, notices: &Mutex<Vec<String>>) {
    if let Ok(mut notices) = notices.lock() {
        for notice in notices.drain(..) {
            if summary.warnings.len() < 16 {
                summary.warnings.push(notice);
            }
        }
    }
}
async fn was_cancelled(cancel: &Cancellation, id: u32) -> bool {
    cancel.state.lock().await.cancelled == Some(u64::from(id))
}
async fn start(
    tx: &Transaction<'_>,
    sql: String,
    options: &QueryOptions,
    max: usize,
    cancel: &Cancellation,
    notices: &Mutex<Vec<String>>,
) -> Result<(Active, Started)> {
    clear_notices(notices);
    let id = begin_generation(cancel).await?;
    let deadline = options
        .timeout
        .and_then(|duration| std::time::Instant::now().checked_add(duration));
    running(cancel, true).await?;
    let result = async {
        let timeout = options
            .timeout
            .map_or(0, |n| n.as_millis().clamp(1, i32::MAX as u128));
        tx.execute(
            "SELECT set_config('statement_timeout', $1, true)",
            &[&timeout.to_string()],
        )
        .await
        .map_err(normalize)?;
        // PostgreSQL Parse rejects multiple statements before Bind/Execute can write.
        let statement = tx.prepare(&sql).await.map_err(normalize)?;
        if !statement.params().is_empty() {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "Query parameters are not bound",
            ));
        }
        // Account the owned schema before allocation or execution. PostgreSQL
        // reports column origins, but they do not prove result nullability: outer
        // joins and grouping can introduce NULL into NOT NULL source columns.
        let needed = statement
            .columns()
            .iter()
            .try_fold(std::mem::size_of::<Vec<Column>>(), |n, c| {
                n.checked_add(std::mem::size_of::<Column>())?
                    .checked_add(c.name().len())?
                    .checked_add(c.type_().name().len())?
                    .checked_add(if c.type_().oid() == 1184 { 3 } else { 0 })
            })
            .ok_or_else(resource)?;
        if needed > max {
            return Err(resource());
        }
        let columns: Vec<Column> = statement
            .columns()
            .iter()
            .map(|c| {
                let typmod = c.type_modifier();
                let (precision, scale) = match c.type_().oid() {
                    1700 if typmod >= 4 => {
                        let packed = typmod - 4;
                        (
                            Some(((packed >> 16) & 0xffff) as u32),
                            Some(((packed & 0x7ff) ^ 1024) - 1024),
                        )
                    }
                    1042 | 1043 if typmod >= 4 => (Some((typmod - 4) as u32), None),
                    1083 | 1114 | 1184 | 1266 if typmod >= 0 => (Some(typmod as u32), None),
                    _ => (None, None),
                };
                Column {
                    name: c.name().into(),
                    database_type: c.type_().name().into(),
                    precision,
                    scale,
                    timezone: (c.type_().oid() == 1184).then(|| "UTC".into()),
                    nullable: None,
                }
            })
            .collect();
        let portal = tx.bind(&statement, &[]).await.map_err(normalize)?;
        let spool = tokio::task::spawn_blocking(move || spool::Spool::new(id))
            .await
            .map_err(|_| DriverError::new(ErrorKind::Internal, "Spool worker failed"))??;
        let reader = spool.reader();
        let mut active = Active {
            deadline,
            id,
            portal,
            spool: Some(spool),
            index: 0,
            summary: QuerySummary::default(),
            completed: false,
            lookahead: None,
        };
        // Execute a bounded first row (or the complete zero-column command) before
        // returning. Merely binding would silently lose writes committed without fetch.
        active.lookahead = next(
            tx,
            &mut active,
            max.saturating_sub(std::mem::size_of::<ResultPage>()),
            cancel,
        )
        .await?;
        collect_notices(&mut active.summary, notices);
        let summary = active.summary.clone();
        Ok((
            active,
            Started {
                id,
                columns,
                reader,
                summary,
            },
        ))
    }
    .await;
    let _ = running(cancel, false).await;
    match result {
        Err(error)
            if error.kind == ErrorKind::Cancelled
                && deadline.is_some_and(|deadline| std::time::Instant::now() >= deadline) =>
        {
            let state = cancel.state.lock().await;
            if state.cancelled == Some(state.current) {
                Err(error)
            } else {
                Err(DriverError::new(ErrorKind::Timeout, "Query timed out").with_code("57014"))
            }
        }
        other => other,
    }
}
async fn start_implicit(
    client: &tokio_postgres::Client,
    sql: String,
    options: &QueryOptions,
    max: usize,
    cancel: &Cancellation,
    notices: &Mutex<Vec<String>>,
) -> Result<(Started, (u32, u64, QuerySummary))> {
    clear_notices(notices);
    let id = begin_generation(cancel).await?;
    let deadline = options
        .timeout
        .and_then(|duration| std::time::Instant::now().checked_add(duration));
    running(cancel, true).await?;
    let timeout = options.timeout.map_or(0, |duration| {
        duration.as_millis().clamp(1, i32::MAX as u128)
    });
    let result = async {
        client
            .execute(
                "SELECT set_config('statement_timeout', $1, false)",
                &[&timeout.to_string()],
            )
            .await
            .map_err(normalize)?;
        let statement = client.prepare(&sql).await.map_err(normalize)?;
        if !statement.params().is_empty() {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "Query parameters are not bound",
            ));
        }
        if !statement.columns().is_empty() {
            return Err(DriverError::new(
                ErrorKind::Internal,
                "Implicit PostgreSQL command unexpectedly returns columns",
            ));
        }
        if max < std::mem::size_of::<Vec<Column>>() {
            return Err(resource());
        }
        // Allocate every fallible local result resource before executing a write.
        let spool = tokio::task::spawn_blocking(move || spool::Spool::new(id))
            .await
            .map_err(|_| DriverError::new(ErrorKind::Internal, "Spool worker failed"))??;
        let reader = spool.reader();
        let affected = client.execute(&statement, &[]).await.map_err(normalize)?;
        let mut summary = QuerySummary {
            transaction_active: Some(false),
            affected_rows: Some(affected),
            ..Default::default()
        };
        collect_notices(&mut summary, notices);
        Ok((
            Started {
                id,
                columns: Vec::new(),
                reader,
                summary: summary.clone(),
            },
            (id, 0, summary),
        ))
    }
    .await;
    let reset = client
        .execute("SELECT set_config('statement_timeout', '0', false)", &[])
        .await
        .map(|_| ())
        .map_err(normalize);
    let _ = running(cancel, false).await;
    let result = match result {
        Err(error)
            if error.kind == ErrorKind::Cancelled
                && deadline.is_some_and(|deadline| std::time::Instant::now() >= deadline) =>
        {
            let state = cancel.state.lock().await;
            if state.cancelled == Some(state.current) {
                Err(error)
            } else {
                Err(DriverError::new(ErrorKind::Timeout, "Query timed out").with_code("57014"))
            }
        }
        other => other,
    };
    match (result, reset) {
        (Ok(value), Ok(())) => Ok(value),
        (Err(error), _) => Err(error),
        (Ok(_), Err(error)) => Err(error),
    }
}
// Borrow the wire field, avoiding a second copy before bounded decoding/spooling.
struct Raw<'a>(Option<&'a [u8]>);
impl<'a> tokio_postgres::types::FromSql<'a> for Raw<'a> {
    fn from_sql(
        _: &tokio_postgres::types::Type,
        raw: &'a [u8],
    ) -> std::result::Result<Self, Box<dyn std::error::Error + Send + Sync>> {
        Ok(Self(Some(raw)))
    }
    fn from_sql_null(
        _: &tokio_postgres::types::Type,
    ) -> std::result::Result<Self, Box<dyn std::error::Error + Send + Sync>> {
        Ok(Self(None))
    }
    fn accepts(_: &tokio_postgres::types::Type) -> bool {
        true
    }
}
fn decode(row: tokio_postgres::Row, spool: &mut spool::Spool, max: usize) -> Result<Row> {
    let base = row
        .len()
        .checked_mul(std::mem::size_of::<Value>())
        .and_then(|n| n.checked_add(std::mem::size_of::<Row>()))
        .ok_or_else(resource)?;
    if base > max {
        return Err(resource());
    }
    let mut remaining = max - base;
    let mut values = Vec::with_capacity(row.len());
    for (index, column) in row.columns().iter().enumerate() {
        let raw: Raw<'_> = row.try_get(index).map_err(normalize)?;
        let oid = column.type_().oid();
        let value = if let Some(bytes) = raw.0.filter(|b| b.len() > remaining.min(64 * 1024)) {
            let (payload, text) = match oid {
                17 => (bytes, false),
                25 | 1042 | 1043 | 19 | 18 | 114 | 142 => (bytes, true),
                3802 if bytes.first() == Some(&1) => (&bytes[1..], true),
                _ => return Err(resource()),
            };
            if text && std::str::from_utf8(payload).is_err() {
                return Err(DriverError::new(
                    ErrorKind::Query,
                    "Invalid PostgreSQL UTF-8 value",
                ));
            }
            if column.type_().name().len() > remaining {
                return Err(resource());
            }
            let mut value = spool.store(payload, text)?;
            if let Value::Deferred { database_type, .. } = &mut value {
                *database_type = column.type_().name().into();
            }
            value
        } else {
            choscordb_postgres_values::decode_bounded(oid, raw.0, remaining)?
        };
        let payload = value.estimated_bytes() - std::mem::size_of::<Value>();
        remaining = remaining.checked_sub(payload).ok_or_else(resource)?;
        values.push(value);
    }
    Ok(values)
}
async fn next(
    tx: &Transaction<'_>,
    active: &mut Active,
    max: usize,
    cancel: &Cancellation,
) -> Result<Option<Row>> {
    if active.completed {
        return Ok(None);
    }
    if active
        .deadline
        .is_some_and(|deadline| std::time::Instant::now() >= deadline)
    {
        return Err(DriverError::new(ErrorKind::Timeout, "Query timed out"));
    }
    running(cancel, true).await?;
    let result = async {
        let stream = tx
            .query_portal_raw(&active.portal, 1)
            .await
            .map_err(normalize)?;
        futures_util::pin_mut!(stream);
        let row = stream.try_next().await.map_err(normalize)?;
        if row.is_some() && stream.try_next().await.map_err(normalize)?.is_some() {
            return Err(DriverError::new(
                ErrorKind::Internal,
                "Portal exceeded row limit",
            ));
        }
        if let Some(affected) = stream.rows_affected() {
            active.completed = true;
            active.summary.affected_rows = Some(affected);
        }
        let Some(row) = row else { return Ok(None) };
        let mut spool = active.spool.take().ok_or_else(stale)?;
        let (returned, value) = tokio::task::spawn_blocking(move || {
            let result = decode(row, &mut spool, max);
            (spool, result)
        })
        .await
        .map_err(|_| DriverError::new(ErrorKind::Internal, "PostgreSQL value worker failed"))?;
        active.spool = Some(returned);
        value.map(Some)
    }
    .await;
    let _ = running(cancel, false).await;
    match result {
        Err(error)
            if error.kind == ErrorKind::Cancelled
                && active
                    .deadline
                    .is_some_and(|deadline| std::time::Instant::now() >= deadline) =>
        {
            let state = cancel.state.lock().await;
            if state.cancelled == Some(state.current) {
                Err(error)
            } else {
                Err(DriverError::new(ErrorKind::Timeout, "Query timed out").with_code("57014"))
            }
        }
        other => other,
    }
}
async fn fetch(
    tx: &Transaction<'_>,
    active: &mut Active,
    size: PageSize,
    max: usize,
    cancel: &Cancellation,
    notices: &Mutex<Vec<String>>,
) -> Result<Fetched> {
    let overhead = std::mem::size_of::<ResultPage>();
    if max <= overhead {
        return Err(resource());
    }
    let row_max = max - overhead;
    let mut rows = Vec::new();
    let mut used = overhead;
    while rows.len() < (size.get() as usize) {
        let row = if let Some(row) = active.lookahead.take() {
            Some(row)
        } else {
            next(tx, active, row_max, cancel).await?
        };
        let Some(row) = row else { break };
        let bytes = std::mem::size_of::<Row>()
            + row.capacity() * std::mem::size_of::<Value>()
            + row
                .iter()
                .map(|v| v.estimated_bytes() - std::mem::size_of::<Value>())
                .sum::<usize>();
        if bytes > row_max {
            return Err(resource());
        }
        if used.saturating_add(bytes) > max {
            active.lookahead = Some(row);
            break;
        }
        rows.reserve_exact(1);
        rows.push(row);
        used += bytes;
    }
    if rows.len() == size.get() as usize && !active.completed && active.lookahead.is_none() {
        active.lookahead = next(tx, active, row_max, cancel).await?;
    }
    collect_notices(&mut active.summary, notices);
    let page = ResultPage {
        index: active.index,
        rows,
        has_more: active.lookahead.is_some() || !active.completed,
    };
    active.index += 1;
    if page.estimated_bytes() > max {
        return Err(resource());
    }
    Ok(Fetched {
        page,
        summary: active.summary.clone(),
    })
}
fn dispose(active: Option<Active>) {
    if let Some(active) = active {
        tokio::task::spawn_blocking(move || drop(active));
    }
}
// The transaction borrows Client only inside this loop; no self-referential state.
pub(super) async fn run(
    mut client: tokio_postgres::Client,
    mut rx: mpsc::Receiver<Command>,
    cancel: Arc<Cancellation>,
    notices: Arc<Mutex<Vec<String>>>,
    pump: tokio::task::JoinHandle<()>,
) {
    let mut pending = None;
    let mut completed: Option<(u32, u64, QuerySummary)> = None;
    'connection: loop {
        let command = if let Some(command) = pending.take() {
            command
        } else {
            let Some(command) = rx.recv().await else {
                break;
            };
            command
        };
        let Command::Execute(sql, options, max, reply) = command else {
            match command {
                Command::Metadata(parent, reply) => {
                    clear_notices(&notices);
                    let result = cancel
                        .closing
                        .auxiliary(&pump, metadata::load_metadata(&client, parent))
                        .await;
                    clear_notices(&notices);
                    let _ = reply.send(result);
                }
                Command::Ddl(object, reply) => {
                    clear_notices(&notices);
                    let result = cancel
                        .closing
                        .auxiliary(&pump, metadata::object_ddl(&client, &object))
                        .await;
                    clear_notices(&notices);
                    let _ = reply.send(result);
                }
                Command::Transaction(_, reply) | Command::Finish(_, reply) => {
                    let _ = reply.send(Ok(()));
                }
                Command::Fetch(id, _, max, reply) => {
                    let result = match completed.as_mut().filter(|(query, _, _)| *query == id) {
                        Some((_, index, summary)) if max >= std::mem::size_of::<ResultPage>() => {
                            let page = ResultPage {
                                index: *index,
                                rows: Vec::new(),
                                has_more: false,
                            };
                            *index += 1;
                            Ok(Fetched {
                                page,
                                summary: summary.clone(),
                            })
                        }
                        _ => Err(stale()),
                    };
                    let _ = reply.send(result);
                }
                Command::Close(reply) => {
                    let _ = reply.send(Ok(()));
                    break;
                }
                Command::Execute(..) => unreachable!(),
            }
            continue;
        };
        completed = None;
        if options.auto_commit && requires_implicit_transaction(&sql) {
            match cancel
                .closing
                .auxiliary(
                    &pump,
                    start_implicit(&client, sql, &options, max, &cancel, &notices),
                )
                .await
            {
                Ok((started, finished)) => {
                    completed = Some(finished);
                    let _ = reply.send(Ok(started));
                }
                Err(error) => {
                    let _ = reply.send(Err(error));
                }
            }
            continue;
        }
        let transaction = match client.transaction().await {
            Ok(tx) => tx,
            Err(error) => {
                let _ = reply.send(Err(normalize(error)));
                continue;
            }
        };
        let auto = options.auto_commit;
        let mut active = match start(&transaction, sql, &options, max, &cancel, &notices).await {
            Ok((active, started)) => {
                if auto && active.completed && active.lookahead.is_none() {
                    let finished = (active.id, 0, active.summary.clone());
                    dispose(Some(active));
                    match transaction.commit().await {
                        Ok(()) => {
                            completed = Some(finished);
                            let _ = reply.send(Ok(started));
                        }
                        Err(error) => {
                            let _ = reply.send(Err(normalize(error)));
                        }
                    }
                    continue;
                }
                let _ = reply.send(Ok(started));
                Some(active)
            }
            Err(error) => {
                let _ = reply.send(Err(error));
                let _ = transaction.rollback().await;
                continue;
            }
        };
        loop {
            let Some(command) = rx.recv().await else {
                dispose(active.take());
                let _ = transaction.rollback().await;
                break 'connection;
            };
            match command {
                Command::Execute(sql, options, max, reply) => {
                    let cancelled = if let Some(active) = active.as_ref() {
                        was_cancelled(&cancel, active.id).await
                    } else {
                        false
                    };
                    dispose(active.take());
                    if auto {
                        let result = if cancelled {
                            transaction.rollback().await
                        } else {
                            transaction.commit().await
                        }
                        .map_err(normalize);
                        if let Err(error) = result {
                            let _ = reply.send(Err(error));
                        } else {
                            pending = Some(Command::Execute(sql, options, max, reply));
                        }
                        break;
                    }
                    match start(&transaction, sql, &options, max, &cancel, &notices).await {
                        Ok((next, started)) => {
                            active = Some(next);
                            let _ = reply.send(Ok(started));
                        }
                        Err(error) => {
                            let _ = reply.send(Err(error));
                        }
                    }
                }
                Command::Fetch(id, size, max, reply) => {
                    // An old cursor is not a failure of the current transaction.
                    if active.as_ref().is_none_or(|active| active.id != id) {
                        let _ = reply.send(Err(stale()));
                        continue;
                    }
                    let result = match active.as_mut().filter(|a| a.id == id) {
                        Some(active) => {
                            fetch(&transaction, active, size, max, &cancel, &notices).await
                        }
                        None => Err(stale()),
                    };
                    let end = auto && result.as_ref().is_ok_and(|f| !f.page.has_more);
                    let failed = auto && result.is_err();
                    if end || failed {
                        dispose(active.take());
                        let finish = if failed {
                            transaction.rollback().await
                        } else {
                            transaction.commit().await
                        };
                        let _ = reply.send(match finish {
                            Ok(()) => result,
                            Err(error) => Err(normalize(error)),
                        });
                        break;
                    }
                    let _ = reply.send(result);
                }
                Command::Finish(id, reply) => {
                    if active.as_ref().is_some_and(|a| a.id == id) {
                        let cancelled = was_cancelled(&cancel, id).await;
                        dispose(active.take());
                        if auto {
                            let result = if cancelled {
                                transaction.rollback().await
                            } else {
                                transaction.commit().await
                            };
                            let _ = reply.send(result.map_err(normalize));
                            break;
                        }
                    }
                    let _ = reply.send(Ok(()));
                }
                Command::Metadata(parent, reply) => {
                    clear_notices(&notices);
                    let result = cancel
                        .closing
                        .auxiliary(&pump, metadata::load_metadata(&transaction, parent))
                        .await;
                    clear_notices(&notices);
                    let _ = reply.send(result);
                }
                Command::Ddl(object, reply) => {
                    clear_notices(&notices);
                    let result = cancel
                        .closing
                        .auxiliary(&pump, metadata::object_ddl(&transaction, &object))
                        .await;
                    clear_notices(&notices);
                    let _ = reply.send(result);
                }
                Command::Transaction(commit, reply) => {
                    dispose(active.take());
                    let result = if commit {
                        transaction.commit().await
                    } else {
                        transaction.rollback().await
                    };
                    let _ = reply.send(result.map_err(normalize));
                    break;
                }
                Command::Close(reply) => {
                    dispose(active.take());
                    let result = if cancel
                        .closing
                        .transport_aborted
                        .load(std::sync::atomic::Ordering::Acquire)
                    {
                        drop(transaction);
                        Ok(())
                    } else {
                        transaction.rollback().await.map_err(normalize)
                    };
                    let _ = reply.send(result);
                    break 'connection;
                }
            }
        }
    }
    drop(client);
    pump.abort();
}

#[cfg(test)]
mod tests {
    use super::requires_implicit_transaction;

    #[test]
    fn classifies_commands_forbidden_in_explicit_transactions() {
        for sql in [
            "VACUUM",
            " -- maintenance\n VACUUM (ANALYZE) public.items",
            "CREATE /* outside */ DATABASE scratch",
            "DROP DATABASE scratch",
            "CREATE TABLESPACE fast LOCATION '/tmp/fast'",
            "DROP TABLESPACE fast",
            "ALTER SYSTEM SET work_mem = '4MB'",
            "REINDEX DATABASE app",
            "REINDEX SYSTEM app",
            "REINDEX SCHEMA public",
            "CREATE INDEX CONCURRENTLY by_id ON items(id)",
            "DROP INDEX CONCURRENTLY by_id",
            "REINDEX INDEX CONCURRENTLY by_id",
            "CLUSTER",
            "REFRESH MATERIALIZED VIEW CONCURRENTLY report",
            "DISCARD ALL",
            "CHECKPOINT",
        ] {
            assert!(requires_implicit_transaction(sql), "{sql}");
        }
    }

    #[test]
    fn keeps_result_and_manual_safe_statements_on_the_portal_path() {
        for sql in [
            "SELECT 'VACUUM'",
            "/* VACUUM */ SELECT 1",
            "CREATE TABLE items(id integer)",
            "DROP TABLE items",
            "ALTER TABLE items ADD COLUMN name text",
            "WITH rows AS (SELECT 1) SELECT * FROM rows",
        ] {
            assert!(!requires_implicit_transaction(sql), "{sql}");
        }
    }
}
