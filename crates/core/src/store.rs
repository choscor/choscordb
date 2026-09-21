//! Ownership moves to the blocking pool for file operations and normal cleanup.
//! Shutdown may reject cleanup tasks and dispose their captures on the submitting
//! thread; strict off-thread cleanup and a disk shutdown deadline remain unproven.
//! The actor awaits one operation at a time; no queue of page copies is accumulated.
use choscordb_driver_api::{Column, DriverError, ErrorKind, ResultPage, Row, Value};
use choscordb_result_store::{ResultStore, StoreConfig, StoreError, StoredPage};

pub(crate) struct Store(Option<ResultStore>);
struct SortState {
    connection: rusqlite::Connection,
    _file: tempfile::NamedTempFile,
    rows: u64,
    max_disk_bytes: u64,
}
pub(crate) struct SortedRows(Option<SortState>);
fn error(error: StoreError) -> DriverError {
    DriverError::new(
        if matches!(error, StoreError::LimitExceeded) {
            ErrorKind::ResourceLimit
        } else {
            ErrorKind::Io
        },
        format!(
            "Original result storage failed: {error}. SQL was already executed; do not retry a write automatically."
        ),
    )
}
fn unavailable() -> DriverError {
    DriverError::new(
        ErrorKind::Internal,
        "Original result storage is unavailable after a worker failure. SQL may already have executed; do not retry a write automatically.",
    )
}
fn sort_error() -> DriverError {
    DriverError::new(ErrorKind::Io, "Temporary result sorting failed")
}
fn sort_limit() -> DriverError {
    DriverError::new(
        ErrorKind::ResourceLimit,
        "Temporary result sorting exceeded the result-store disk budget",
    )
}
fn sort_sql_error(error: rusqlite::Error) -> DriverError {
    if matches!(error, rusqlite::Error::SqliteFailure(inner, _) if inner.code == rusqlite::ErrorCode::DiskFull)
    {
        sort_limit()
    } else {
        sort_error()
    }
}
impl Store {
    #[cfg(test)]
    pub async fn new(
        columns: Vec<Column>,
        config: StoreConfig,
        directory: Option<std::path::PathBuf>,
    ) -> Result<Self, DriverError> {
        Self::create_reserved(columns, config, directory, ())
            .await
            .map(|(store, ())| store)
    }
    pub async fn create_reserved<T: Send + 'static>(
        columns: Vec<Column>,
        config: StoreConfig,
        directory: Option<std::path::PathBuf>,
        reservation: T,
    ) -> Result<(Self, T), DriverError> {
        tokio::task::spawn_blocking(move || {
            directory
                .map_or_else(
                    || ResultStore::new(&columns, config.clone()),
                    |directory| ResultStore::in_directory(directory, &columns, config.clone()),
                )
                .map(|store| (Self(Some(store)), reservation))
                .map_err(error)
        })
        .await
        .map_err(|_| unavailable())?
    }
    pub fn count(&self) -> Result<u64, DriverError> {
        self.0
            .as_ref()
            .map(ResultStore::page_count)
            .ok_or_else(unavailable)
    }
    pub fn rows(&self) -> Result<u64, DriverError> {
        self.0
            .as_ref()
            .map(ResultStore::row_count)
            .ok_or_else(unavailable)
    }
    pub fn disk_bytes(&self) -> Result<u64, DriverError> {
        self.0
            .as_ref()
            .map(ResultStore::disk_bytes)
            .ok_or_else(unavailable)
    }
    async fn operate<T: Send + 'static>(
        &mut self,
        operation: impl FnOnce(&mut ResultStore) -> Result<T, DriverError> + Send + 'static,
    ) -> Result<T, DriverError> {
        let store = self.0.take().ok_or_else(unavailable)?;
        let owned = Self(Some(store));
        let (mut returned, result) = tokio::task::spawn_blocking(move || {
            let mut owned = owned;
            let result = owned.0.as_mut().ok_or_else(unavailable).and_then(operation);
            (owned, result)
        })
        .await
        .map_err(|_| unavailable())?;
        self.0 = returned.0.take();
        result
    }
    pub async fn schema_reserved<T: Send + 'static>(
        &mut self,
        reservation: T,
    ) -> Result<(Vec<Column>, T), DriverError> {
        self.operate(move |store| {
            store
                .schema()
                .map(|columns| (columns, reservation))
                .map_err(error)
        })
        .await
    }
    pub async fn append_reserved<T: Send + 'static>(
        &mut self,
        page: ResultPage,
        lease: T,
    ) -> Result<(StoredPage, T), DriverError> {
        self.operate(move |store| {
            let first_row = store.row_count();
            store.append(&page).map_err(error)?;
            Ok((StoredPage { first_row, page }, lease))
        })
        .await
    }
    pub async fn read_reserved<T: Send + 'static>(
        &mut self,
        index: u64,
        lease: T,
    ) -> Result<(StoredPage, T), DriverError> {
        self.operate(move |store| {
            store
                .read_page(index)
                .map(|page| (page, lease))
                .map_err(error)
        })
        .await
    }
    pub async fn dispose(mut self) {
        if let Some(store) = self.0.take() {
            let _ = tokio::task::spawn_blocking(move || drop(store)).await;
        }
    }
    #[cfg(test)]
    pub async fn append(&mut self, page: ResultPage) -> Result<StoredPage, DriverError> {
        self.operate(move |store| {
            let first_row = store.row_count();
            store.append(&page).map_err(error)?;
            Ok(StoredPage { first_row, page })
        })
        .await
    }
    #[cfg(test)]
    pub async fn read(&mut self, index: u64) -> Result<StoredPage, DriverError> {
        self.operate(move |store| store.read_page(index).map_err(error))
            .await
    }
}
impl Drop for Store {
    fn drop(&mut self) {
        if let Some(store) = self.0.take() {
            tokio::task::spawn_blocking(move || drop(store));
        }
    }
}

impl SortedRows {
    pub async fn new(
        direction: crate::SortDirection,
        max_disk_bytes: u64,
        cache_bytes: usize,
        directory: Option<std::path::PathBuf>,
    ) -> Result<Self, DriverError> {
        tokio::task::spawn_blocking(move || {
            let file = directory.map_or_else(tempfile::NamedTempFile::new, tempfile::NamedTempFile::new_in).map_err(|_| sort_error())?;
            let connection = rusqlite::Connection::open(file.path()).map_err(|_| sort_error())?;
            connection.execute_batch("PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF; PRAGMA temp_store=FILE;").map_err(|_| sort_error())?;
            connection.pragma_update(None, "cache_size", -i64::try_from(cache_bytes.div_ceil(1024).max(1)).map_err(|_| sort_limit())?).map_err(|_| sort_error())?;
            let sqlite_page_bytes: i64 = connection
                .query_row("PRAGMA page_size", [], |row| row.get(0))
                .map_err(|_| sort_error())?;
            if max_disk_bytes < u64::try_from(sqlite_page_bytes).unwrap_or(u64::MAX) {
                return Err(sort_limit());
            }
            let maximum_pages = i64::try_from(max_disk_bytes)
                .unwrap_or(i64::MAX)
                .checked_div(sqlite_page_bytes.max(1))
                .unwrap_or(1)
                .max(1);
            connection
                .pragma_update(None, "max_page_count", maximum_pages)
                .map_err(|_| sort_limit())?;
            connection.create_collation("typed_result_value", move |left, right| {
                let left = serde_json::from_str::<Value>(left);
                let right = serde_json::from_str::<Value>(right);
                match (left, right) {
                    (Ok(left), Ok(right)) => crate::result_view::sort_compare(&left, &right, direction).unwrap_or(std::cmp::Ordering::Equal),
                    _ => std::cmp::Ordering::Equal,
                }
            }).map_err(|_| sort_error())?;
            connection.execute_batch("CREATE TABLE sorted_rows(sequence INTEGER PRIMARY KEY, sort_key TEXT NOT NULL, row_json BLOB NOT NULL); CREATE INDEX sorted_order ON sorted_rows(sort_key COLLATE typed_result_value, sequence);").map_err(sort_sql_error)?;
            if file.as_file().metadata().map_err(|_| sort_error())?.len() > max_disk_bytes {
                return Err(sort_limit());
            }
            Ok(Self(Some(SortState { connection, _file: file, rows: 0, max_disk_bytes })))
        }).await.map_err(|_| unavailable())?
    }

    async fn operate<T: Send + 'static>(
        &mut self,
        operation: impl FnOnce(&mut SortState) -> Result<T, DriverError> + Send + 'static,
    ) -> Result<T, DriverError> {
        let mut state = self.0.take().ok_or_else(unavailable)?;
        let (state, result) = tokio::task::spawn_blocking(move || {
            let result = operation(&mut state);
            (state, result)
        })
        .await
        .map_err(|_| unavailable())?;
        self.0 = Some(state);
        result
    }

    pub async fn insert(&mut self, rows: Vec<(Value, Row)>) -> Result<(), DriverError> {
        self.operate(move |state| {
            let transaction = state.connection.transaction().map_err(|_| sort_error())?;
            {
                let mut statement = transaction
                    .prepare_cached(
                        "INSERT INTO sorted_rows(sequence,sort_key,row_json) VALUES(?1,?2,?3)",
                    )
                    .map_err(|_| sort_error())?;
                for (sort_key, row) in rows {
                    let sequence = i64::try_from(state.rows).map_err(|_| sort_limit())?;
                    let sort_key = serde_json::to_string(&sort_key).map_err(|_| sort_error())?;
                    let row = serde_json::to_vec(&row).map_err(|_| sort_error())?;
                    statement
                        .execute(rusqlite::params![sequence, sort_key, row])
                        .map_err(sort_sql_error)?;
                    state.rows = state.rows.checked_add(1).ok_or_else(sort_limit)?;
                }
            }
            transaction.commit().map_err(sort_sql_error)?;
            if state
                ._file
                .as_file()
                .metadata()
                .map_err(|_| sort_error())?
                .len()
                > state.max_disk_bytes
            {
                return Err(sort_limit());
            }
            Ok(())
        })
        .await
    }

    pub async fn finish(
        mut self,
        columns: Vec<Column>,
        mut config: StoreConfig,
        directory: Option<std::path::PathBuf>,
        page_size: usize,
        cancellation: tokio::sync::watch::Receiver<bool>,
    ) -> Result<Store, DriverError> {
        let state = self.0.take().ok_or_else(unavailable)?;
        tokio::task::spawn_blocking(move || {
            let SortState {
                connection,
                _file,
                max_disk_bytes,
                ..
            } = state;
            let cancelled = cancellation.clone();
            connection
                .progress_handler(1000, Some(move || *cancelled.borrow()))
                .map_err(|_| sort_error())?;
            if _file
                .as_file()
                .metadata()
                .map_err(|_| sort_error())?
                .len()
                > max_disk_bytes
            {
                return Err(sort_limit());
            }
            let sorter_bytes = _file
                .as_file()
                .metadata()
                .map_err(|_| sort_error())?
                .len();
            config.max_disk_bytes = config
                .max_disk_bytes
                .min(max_disk_bytes.saturating_sub(sorter_bytes));
            let mut result = directory.map_or_else(
                || ResultStore::new(&columns, config.clone()),
                |directory| ResultStore::in_directory(directory, &columns, config.clone()),
            ).map_err(error)?;
            let decoded_limit = config.max_page_decoded_bytes;
            let mut statement = connection.prepare("SELECT row_json FROM sorted_rows ORDER BY sort_key COLLATE typed_result_value ASC, sequence ASC").map_err(|_| if *cancellation.borrow() { DriverError::new(ErrorKind::Cancelled, "Result view cancelled") } else { sort_error() })?;
            let mut ordered = statement.query([]).map_err(|_| if *cancellation.borrow() { DriverError::new(ErrorKind::Cancelled, "Result view cancelled") } else { sort_error() })?;
            let mut page_rows = Vec::with_capacity(page_size);
            let mut index = 0_u64;
            loop {
                let next = ordered.next().map_err(|_| if *cancellation.borrow() { DriverError::new(ErrorKind::Cancelled, "Result view cancelled") } else { sort_error() })?;
                let Some(row) = next else { break };
                let bytes: Vec<u8> = row.get(0).map_err(|_| sort_error())?;
                if page_rows.len() == page_size {
                    let full = std::mem::replace(&mut page_rows, Vec::with_capacity(page_size));
                    result.append(&ResultPage { index, rows: full, has_more: true }).map_err(error)?;
                    index += 1;
                }
                page_rows.push(serde_json::from_slice::<Row>(&bytes).map_err(|_| sort_error())?);
                if page_rows.len() > 1
                    && crate::result_view::page_estimated_bytes(&page_rows) > decoded_limit
                {
                    let overflow = page_rows.pop().expect("page is nonempty");
                    let full = std::mem::replace(&mut page_rows, Vec::with_capacity(page_size));
                    result.append(&ResultPage { index, rows: full, has_more: true }).map_err(error)?;
                    index += 1;
                    page_rows.push(overflow);
                }
            }
            result.append(&ResultPage { index, rows: page_rows, has_more: false }).map_err(error)?;
            Ok(Store(Some(result)))
        }).await.map_err(|_| unavailable())?
    }
}
impl Drop for SortedRows {
    fn drop(&mut self) {
        if let Some(state) = self.0.take() {
            tokio::task::spawn_blocking(move || drop(state));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn missing_worker_ownership_returns_an_error_without_panicking() {
        let mut store = Store(None);
        assert!(matches!(store.read(0).await, Err(error) if error.kind == ErrorKind::Internal));
    }
    #[tokio::test]
    async fn panicked_worker_poison_is_reported_on_all_subsequent_operations() {
        let mut store = Store::new(vec![], StoreConfig::default(), None)
            .await
            .unwrap();
        let failed: Result<(), DriverError> =
            store.operate(|_| panic!("injected worker failure")).await;
        assert!(matches!(failed, Err(error) if error.kind == ErrorKind::Internal));
        assert!(matches!(store.count(), Err(error) if error.kind == ErrorKind::Internal));
        assert!(matches!(store.read(0).await, Err(error) if error.kind == ErrorKind::Internal));
        let page = ResultPage {
            index: 0,
            rows: vec![],
            has_more: false,
        };
        assert!(
            matches!(store.append(page).await, Err(error) if error.kind == ErrorKind::Internal)
        );
    }
}
