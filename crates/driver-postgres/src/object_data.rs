//! Bounded reads borrow the worker's current client/transaction without replacing SQL.
use super::*;
use futures_util::TryStreamExt;
use std::hash::BuildHasher;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use tokio_postgres::GenericClient;
pub(super) struct Stop {
    cancelled: AtomicBool,
    running: tokio::sync::Mutex<ReadActivity>,
    shared: Arc<Cancellation>,
}
#[derive(Default)]
struct ReadActivity {
    active: bool,
    cancellation_sent: bool,
}
impl ReadActivity {
    async fn cancel(&mut self, send: impl std::future::Future<Output = Result<()>>) -> Result<()> {
        // A second packet can arrive after the SELECT ends and cancel savepoint
        // cleanup or an unrelated SQL portal. Keep failed sends retryable.
        if self.active && !self.cancellation_sent {
            send.await?;
            self.cancellation_sent = true;
        }
        Ok(())
    }
}
#[async_trait]
impl CancelHandle for Stop {
    async fn cancel(&self) -> Result<()> {
        self.cancelled.store(true, Ordering::Release);
        self.running
            .lock()
            .await
            .cancel(self.shared.send_cancel())
            .await
    }
}
pub(super) struct Opened {
    sql: String,
    columns: Vec<Column>,
    spool: spool::Spool,
    summary: QuerySummary,
}
/// The statement and result formats that every page of one object view reuses.
#[derive(Clone)]
struct PreparedRead {
    statement: tokio_postgres::Statement,
    text_overrides: Arc<Vec<Option<String>>>,
    result_formats: Vec<i16>,
}
pub(super) struct Read {
    sql: String,
    limit: i64,
    offset: i64,
    prepared: Arc<Mutex<Option<PreparedRead>>>,
    width: usize,
    max: usize,
    size: PageSize,
    index: u64,
    spool: spool::Spool,
    stop: Arc<Stop>,
}
struct ObjectCursor {
    client: Client,
    source: Opened,
    prepared: Arc<Mutex<Option<PreparedRead>>>,
    offset: u64,
    index: u64,
    stop: Arc<Stop>,
    closed: bool,
}
fn limit() -> DriverError {
    DriverError::new(
        ErrorKind::ResourceLimit,
        "Object data exceeds the display memory budget",
    )
}
fn changed_columns() -> DriverError {
    DriverError::new(
        ErrorKind::StaleHandle,
        "Object columns changed; refresh the object",
    )
}
fn cancelled() -> DriverError {
    DriverError::new(ErrorKind::Cancelled, "Object read cancelled")
}
pub(super) async fn open(
    client: Client,
    shared: Arc<Cancellation>,
    object: &ObjectId,
    max: usize,
) -> Result<Box<dyn ResultCursor>> {
    let object = object.clone();
    let source = client
        .request(|reply| Command::ObjectOpen(object, max, reply))
        .await?;
    Ok(Box::new(ObjectCursor {
        client,
        source,
        prepared: Default::default(),
        offset: 0,
        index: 0,
        stop: Arc::new(Stop {
            cancelled: AtomicBool::new(false),
            running: tokio::sync::Mutex::new(ReadActivity::default()),
            shared,
        }),
        closed: false,
    }))
}
pub(super) async fn prepare<C: GenericClient + Sync>(
    client: &C,
    object: &ObjectId,
    max: usize,
    transaction: bool,
) -> Result<Opened> {
    let raw = object.0.strip_prefix("pg:relation:").ok_or_else(|| {
        DriverError::new(
            ErrorKind::InvalidInput,
            "Object data requires a PostgreSQL relation",
        )
    })?;
    let oid: u32 = raw
        .parse()
        .map_err(|_| DriverError::new(ErrorKind::InvalidInput, "Invalid relation identifier"))?;
    if oid == 0 || raw != oid.to_string() {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "Invalid relation identifier",
        ));
    }
    let row=client.query_opt("SELECT quote_ident(n.nspname)||'.'||quote_ident(c.relname) AS name FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE c.oid=$1 AND c.relkind IN ('r','p','v','m','f')",&[&oid]).await.map_err(normalize)?.ok_or_else(||DriverError::new(ErrorKind::StaleHandle,"PostgreSQL relation no longer exists"))?;
    let name: &str = row.get(0);
    if name.len() > max {
        return Err(limit());
    }
    let sql = format!("SELECT * FROM {name} LIMIT $1 OFFSET $2");
    // Catalog columns avoid acquiring a relation lock during schema preparation.
    // The first SELECT runs in the cancellable bounded-read path below.
    let objects = metadata::load_metadata(client, Some(object.clone())).await?;
    let columns: Vec<Column> = objects.into_iter().filter_map(|o| o.column).collect();
    let bytes = columns
        .iter()
        .try_fold(std::mem::size_of::<Vec<Column>>(), |n, c| {
            n.checked_add(std::mem::size_of::<Column>())?
                .checked_add(c.name.len())?
                .checked_add(c.database_type.len())?
                .checked_add(c.timezone.as_ref().map_or(0, String::len))
        })
        .ok_or_else(limit)?;
    if bytes > max {
        return Err(limit());
    }
    let spool = tokio::task::spawn_blocking(|| spool::Spool::new(1))
        .await
        .map_err(|_| limit())??;
    Ok(Opened {
        sql,
        columns,
        spool,
        summary: QuerySummary {
            transaction_active: Some(transaction),
            ..Default::default()
        },
    })
}
pub(super) async fn read<C: GenericClient + Sync>(
    client: &C,
    request: Read,
    transaction: bool,
    overrides: &mut worker::OverrideCache,
) -> Result<Fetched> {
    // A failed SELECT or native cancellation must not abort the user's existing
    // transaction or invalidate its older SQL portal. This savepoint encloses
    // only our read and is removed before returning control to the user.
    static SAVEPOINT_SEQUENCE: AtomicU64 = AtomicU64::new(0);
    let suffix = std::collections::hash_map::RandomState::new()
        .hash_one(SAVEPOINT_SEQUENCE.fetch_add(1, Ordering::Relaxed));
    let savepoint = format!("\"__choscordb_object_read_{suffix:016x}\"");
    if transaction {
        client
            .batch_execute(&format!("SAVEPOINT {savepoint}"))
            .await
            .map_err(normalize)?;
    }
    let ready = {
        let mut running = request.stop.running.lock().await;
        if request.stop.cancelled.load(Ordering::Acquire) {
            false
        } else {
            running.active = true;
            true
        }
    };
    let result = if ready {
        read_inner(client, &request, overrides).await
    } else {
        Err(cancelled())
    };
    if ready && result.is_err() {
        let _ = request.stop.cancel().await;
    }
    request.stop.running.lock().await.active = false;
    if transaction {
        let cleanup = if result.is_err() {
            format!("ROLLBACK TO SAVEPOINT {savepoint}; RELEASE SAVEPOINT {savepoint}")
        } else {
            format!("RELEASE SAVEPOINT {savepoint}")
        };
        client.batch_execute(&cleanup).await.map_err(normalize)?;
    }
    result.map(|page| Fetched {
        page,
        summary: QuerySummary {
            transaction_active: Some(transaction),
            ..Default::default()
        },
    })
}
fn poisoned() -> DriverError {
    DriverError::new(ErrorKind::Internal, "Object read state is unavailable")
}
async fn prepared_read<C: GenericClient + Sync>(
    client: &C,
    request: &Read,
    overrides: &mut worker::OverrideCache,
) -> Result<PreparedRead> {
    if let Some(prepared) = request.prepared.lock().map_err(|_| poisoned())?.clone() {
        return Ok(prepared);
    }
    let statement = client.prepare(&request.sql).await.map_err(normalize)?;
    let text_overrides = worker::text_overrides(client, &statement, request.max, overrides).await?;
    let result_formats = statement
        .columns()
        .iter()
        .zip(text_overrides.iter())
        .map(|(column, override_type)| {
            if override_type.is_none()
                && choscordb_postgres_values::supports_binary_oid(column.type_().oid())
            {
                1
            } else {
                0
            }
        })
        .collect();
    let prepared = PreparedRead {
        statement,
        text_overrides,
        result_formats,
    };
    *request.prepared.lock().map_err(|_| poisoned())? = Some(prepared.clone());
    Ok(prepared)
}
async fn read_inner<C: GenericClient + Sync>(
    client: &C,
    request: &Read,
    overrides: &mut worker::OverrideCache,
) -> Result<ResultPage> {
    let overhead = std::mem::size_of::<ResultPage>();
    if request.max <= overhead
        || request.width.saturating_mul(std::mem::size_of::<Value>()) > request.max - overhead
    {
        return Err(limit());
    }
    let PreparedRead {
        statement,
        text_overrides,
        result_formats,
    } = prepared_read(client, request, overrides).await?;
    let stream = client
        .client()
        .query_raw_with_result_formats(
            &statement,
            [
                &request.limit as &(dyn tokio_postgres::types::ToSql + Sync),
                &request.offset,
            ],
            &result_formats,
        )
        .await
        .map_err(|error| {
            // PostgreSQL refuses a prepared SELECT * after its columns change.
            if error.code() == Some(&tokio_postgres::error::SqlState::FEATURE_NOT_SUPPORTED) {
                changed_columns()
            } else {
                normalize(error)
            }
        })?;
    futures_util::pin_mut!(stream);
    let mut rows = Vec::with_capacity(request.size.get() as usize);
    let mut used = overhead;
    let mut more = false;
    while let Some(row) = stream.try_next().await.map_err(normalize)? {
        if request.stop.cancelled.load(Ordering::Acquire) {
            return Err(cancelled());
        }
        if row.len() != request.width {
            return Err(changed_columns());
        }
        if more || rows.len() == request.size.get() as usize {
            more = true;
            continue;
        }
        let mut spool = request.spool.clone();
        let text_overrides = text_overrides.clone();
        let max = request.max - overhead;
        let values = tokio::task::spawn_blocking(move || {
            super::worker::decode(row, &mut spool, max, &text_overrides)
        })
        .await
        .map_err(|_| limit())??;
        let bytes = std::mem::size_of::<Row>() + choscordb_driver_api::row_estimated_bytes(&values);
        if used.saturating_add(bytes) > request.max {
            if rows.is_empty() {
                return Err(limit());
            }
            more = true;
            continue;
        }
        rows.push(values);
        used += bytes;
    }
    // The page estimate counts capacity; the budget above counted only the rows.
    rows.shrink_to_fit();
    Ok(ResultPage {
        index: request.index,
        rows,
        has_more: more,
    })
}
#[async_trait]
impl ResultCursor for ObjectCursor {
    fn independent_cancellation_handle(&self) -> Option<Arc<dyn CancelHandle>> {
        Some(self.stop.clone())
    }
    fn deferred_reader(&self) -> Option<Arc<dyn DeferredReader>> {
        Some(self.source.spool.reader())
    }
    fn columns(&self) -> &[Column] {
        &self.source.columns
    }
    fn summary(&self) -> QuerySummary {
        self.source.summary.clone()
    }
    async fn fetch_page_bounded(&mut self, size: PageSize, max: usize) -> Result<ResultPage> {
        if self.closed {
            return Err(DriverError::new(
                ErrorKind::StaleHandle,
                "Object result is closed",
            ));
        }
        let request = Read {
            sql: self.source.sql.clone(),
            limit: i64::from(size.get()) + 1,
            offset: i64::try_from(self.offset).map_err(|_| limit())?,
            prepared: self.prepared.clone(),
            width: self.source.columns.len(),
            max,
            size,
            index: self.index,
            spool: self.source.spool.clone(),
            stop: self.stop.clone(),
        };
        let fetched = self
            .client
            .request(|reply| Command::ObjectRead(request, reply))
            .await?;
        self.source.summary = fetched.summary;
        let page = fetched.page;
        self.offset = self
            .offset
            .checked_add(page.rows.len() as u64)
            .ok_or_else(limit)?;
        self.index = self.index.checked_add(1).ok_or_else(limit)?;
        Ok(page)
    }
    async fn close(&mut self) -> Result<()> {
        self.closed = true;
        Ok(())
    }
}

#[cfg(test)]
mod cancellation_tests {
    use super::*;

    #[tokio::test]
    async fn successful_cancel_is_not_dispatched_again() {
        let mut activity = ReadActivity {
            active: true,
            ..Default::default()
        };
        activity.cancel(async { Ok(()) }).await.unwrap();
        activity
            .cancel(async { panic!("repeated cancellation reached the transport") })
            .await
            .unwrap();
    }

    #[tokio::test]
    async fn idle_read_does_not_cancel_another_operation() {
        ReadActivity::default()
            .cancel(async { panic!("idle cancellation reached the transport") })
            .await
            .unwrap();
    }

    #[tokio::test]
    async fn failed_send_can_be_retried() {
        let mut activity = ReadActivity {
            active: true,
            ..Default::default()
        };
        assert!(
            activity
                .cancel(async {
                    Err(DriverError::new(
                        ErrorKind::Connection,
                        "cancellation transport unavailable",
                    ))
                })
                .await
                .is_err()
        );
        activity.cancel(async { Ok(()) }).await.unwrap();
        activity
            .cancel(async { panic!("successful retry was dispatched again") })
            .await
            .unwrap();
    }
}
