//! Bounded reads borrow the worker's current client/transaction without replacing SQL.
use super::*;
use futures_util::TryStreamExt;
use std::hash::BuildHasher;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use tokio_postgres::GenericClient;
pub(super) struct Stop {
    cancelled: AtomicBool,
    running: tokio::sync::Mutex<bool>,
    shared: Arc<Cancellation>,
}
#[async_trait]
impl CancelHandle for Stop {
    async fn cancel(&self) -> Result<()> {
        self.cancelled.store(true, Ordering::Release);
        let running = self.running.lock().await;
        if *running {
            self.shared.send_cancel().await?;
        }
        Ok(())
    }
}
pub(super) struct Opened {
    sql: String,
    columns: Vec<Column>,
    spool: spool::Spool,
    summary: QuerySummary,
}
pub(super) struct Read {
    sql: String,
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
        offset: 0,
        index: 0,
        stop: Arc::new(Stop {
            cancelled: AtomicBool::new(false),
            running: tokio::sync::Mutex::new(false),
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
    let sql = format!("SELECT * FROM {name}");
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
            *running = true;
            true
        }
    };
    let result = if ready {
        read_inner(client, &request).await
    } else {
        Err(cancelled())
    };
    if ready && result.is_err() {
        let _ = request.stop.cancel().await;
    }
    *request.stop.running.lock().await = false;
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
async fn read_inner<C: GenericClient + Sync>(client: &C, request: &Read) -> Result<ResultPage> {
    let overhead = std::mem::size_of::<ResultPage>();
    if request.max <= overhead
        || request.width.saturating_mul(std::mem::size_of::<Value>()) > request.max - overhead
    {
        return Err(limit());
    }
    let stream = client
        .query_raw(
            &request.sql,
            std::iter::empty::<&(dyn tokio_postgres::types::ToSql + Sync)>(),
        )
        .await
        .map_err(normalize)?;
    futures_util::pin_mut!(stream);
    let mut rows = Vec::new();
    let mut used = overhead;
    let mut more = false;
    while let Some(row) = stream.try_next().await.map_err(normalize)? {
        if request.stop.cancelled.load(Ordering::Acquire) {
            return Err(cancelled());
        }
        if row.len() != request.width {
            return Err(DriverError::new(
                ErrorKind::StaleHandle,
                "Object columns changed; refresh the object",
            ));
        }
        if more || rows.len() == request.size.get() as usize {
            more = true;
            continue;
        }
        let mut spool = request.spool.clone();
        let max = request.max - overhead;
        let values =
            tokio::task::spawn_blocking(move || super::worker::decode(row, &mut spool, max))
                .await
                .map_err(|_| limit())??;
        let bytes = std::mem::size_of::<Row>()
            + values.capacity() * std::mem::size_of::<Value>()
            + values
                .iter()
                .map(|v| v.estimated_bytes() - std::mem::size_of::<Value>())
                .sum::<usize>();
        if used.saturating_add(bytes) > request.max {
            if rows.is_empty() {
                return Err(limit());
            }
            more = true;
            continue;
        }
        rows.reserve_exact(1);
        rows.push(values);
        used += bytes;
    }
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
    async fn fetch_page(&mut self, size: PageSize) -> Result<ResultPage> {
        self.fetch_page_bounded(size, 4 * 1024 * 1024).await
    }
    async fn fetch_page_bounded(&mut self, size: PageSize, max: usize) -> Result<ResultPage> {
        if self.closed {
            return Err(DriverError::new(
                ErrorKind::StaleHandle,
                "Object result is closed",
            ));
        }
        let request = Read {
            sql: format!(
                "{} LIMIT {} OFFSET {}",
                self.source.sql,
                u64::from(size.get()) + 1,
                self.offset
            ),
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
