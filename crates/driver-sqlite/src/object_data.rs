//! Short, bounded reads on the existing session, alongside its suspended SQL cursor.
use super::*;
use rusqlite::Connection as Db;
struct Stop(Arc<AtomicBool>);
#[async_trait]
impl CancelHandle for Stop {
    async fn cancel(&self) -> Result<()> {
        self.0.store(true, Ordering::Release);
        Ok(())
    }
}
struct ObjectCursor {
    client: Client,
    sql: String,
    columns: Vec<Column>,
    spool: spool::Spool,
    stop: Arc<Stop>,
    offset: u64,
    index: u64,
    closed: bool,
    summary: QuerySummary,
}
fn limit() -> DriverError {
    DriverError::new(
        ErrorKind::ResourceLimit,
        "Object data exceeds the display memory budget",
    )
}
fn qualified(object: &ObjectId) -> Result<String> {
    let names: Vec<String> = serde_json::from_str(&object.0).map_err(|_| {
        DriverError::new(ErrorKind::InvalidInput, "Invalid SQLite object identifier")
    })?;
    if names.len() != 2 {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "Object data requires a table or view",
        ));
    }
    Ok(names
        .iter()
        .map(|s| format!("\"{}\"", s.replace('"', "\"\"")))
        .collect::<Vec<_>>()
        .join("."))
}
pub(super) async fn open(
    client: Client,
    object: &ObjectId,
    max: usize,
) -> Result<Box<dyn ResultCursor>> {
    let sql = format!("SELECT * FROM {}", qualified(object)?);
    if sql.len() > max {
        return Err(limit());
    }
    let prepare = sql.clone();
    let (columns, spool, summary) = client
        .request(|reply| {
            Command::Object(Box::new(move |db| {
                let result = (|| {
                    let statement = db
                        .prepare(&format!("{prepare} LIMIT 0"))
                        .map_err(normalize)?;
                    let mut bytes = std::mem::size_of::<Vec<Column>>();
                    if statement
                        .column_count()
                        .saturating_mul(std::mem::size_of::<Column>())
                        > max
                    {
                        return Err(limit());
                    }
                    let borrowed = statement.columns();
                    for column in &borrowed {
                        bytes = bytes
                            .saturating_add(std::mem::size_of::<Column>())
                            .saturating_add(column.name().len())
                            .saturating_add(column.decl_type().unwrap_or("").len());
                        if bytes > max {
                            return Err(limit());
                        }
                    }
                    let columns = borrowed
                        .iter()
                        .map(|column| Column {
                            name: column.name().into(),
                            database_type: column.decl_type().unwrap_or("").into(),
                            precision: None,
                            scale: None,
                            timezone: None,
                            nullable: None,
                        })
                        .collect();
                    Ok((
                        columns,
                        spool::Spool::new(1)?,
                        QuerySummary {
                            transaction_active: Some(!db.is_autocommit()),
                            ..Default::default()
                        },
                    ))
                })();
                let _ = reply.send(result);
            }))
        })
        .await?;
    Ok(Box::new(ObjectCursor {
        client,
        sql,
        columns,
        spool,
        stop: Arc::new(Stop(Arc::new(AtomicBool::new(false)))),
        offset: 0,
        index: 0,
        closed: false,
        summary,
    }))
}
#[async_trait]
impl ResultCursor for ObjectCursor {
    fn independent_cancellation_handle(&self) -> Option<Arc<dyn CancelHandle>> {
        Some(self.stop.clone())
    }
    fn deferred_reader(&self) -> Option<Arc<dyn DeferredReader>> {
        Some(self.spool.reader())
    }
    fn columns(&self) -> &[Column] {
        &self.columns
    }
    fn summary(&self) -> QuerySummary {
        self.summary.clone()
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
        let sql = format!(
            "{} LIMIT {} OFFSET {}",
            self.sql,
            u64::from(size.get()) + 1,
            self.offset
        );
        let mut spool = self.spool.clone();
        let stop = self.stop.0.clone();
        let width = self.columns.len();
        let index = self.index;
        let (page, transaction_active) = self
            .client
            .request(|reply| {
                Command::Object(Box::new(move |db| {
                    let requested = stop.clone();
                    db.progress_handler(1000, Some(move || requested.load(Ordering::Acquire)));
                    let result = read(db, &sql, width, size, max, index, &mut spool, &stop);
                    db.progress_handler(0, None::<fn() -> bool>);
                    let _ = reply.send(result.map(|page| (page, !db.is_autocommit())));
                }))
            })
            .await?;
        self.summary.transaction_active = Some(transaction_active);
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
#[allow(clippy::too_many_arguments)]
fn read(
    db: &Db,
    sql: &str,
    width: usize,
    size: PageSize,
    max: usize,
    index: u64,
    spool: &mut spool::Spool,
    stop: &AtomicBool,
) -> Result<ResultPage> {
    if stop.load(Ordering::Acquire) {
        return Err(DriverError::new(
            ErrorKind::Cancelled,
            "Object read cancelled",
        ));
    }
    let overhead = std::mem::size_of::<ResultPage>();
    let minimum = width
        .checked_mul(std::mem::size_of::<Value>())
        .ok_or_else(limit)?;
    if max <= overhead || minimum > max - overhead {
        return Err(limit());
    }
    let mut statement = db.prepare(sql).map_err(normalize)?;
    if statement.column_count() != width {
        return Err(DriverError::new(
            ErrorKind::StaleHandle,
            "Object columns changed; refresh the object",
        ));
    }
    let mut source = statement.query([]).map_err(normalize)?;
    let mut rows = Vec::new();
    let mut used = overhead;
    let mut has_more = false;
    while let Some(row) = super::worker::next_row(&mut source, width, spool, max - overhead)? {
        if stop.load(Ordering::Acquire) {
            return Err(DriverError::new(
                ErrorKind::Cancelled,
                "Object read cancelled",
            ));
        }
        let bytes = std::mem::size_of::<Row>() + super::worker::row_bytes(&row);
        if used.saturating_add(bytes) > max || rows.len() == size.get() as usize {
            if rows.is_empty() {
                return Err(limit());
            }
            has_more = true;
            break;
        }
        rows.reserve_exact(1);
        rows.push(row);
        used += bytes;
    }
    Ok(ResultPage {
        index,
        rows,
        has_more,
    })
}
