//! Ownership moves to the blocking pool for file operations and normal cleanup.
//! Shutdown may reject cleanup tasks and dispose their captures on the submitting
//! thread; strict off-thread cleanup and a disk shutdown deadline remain unproven.
//! The actor awaits one operation at a time; no queue of page copies is accumulated.
use choscordb_driver_api::{Column, DriverError, ErrorKind, ResultPage};
use choscordb_result_store::{ResultStore, StoreConfig, StoreError, StoredPage};

pub(crate) struct Store(Option<ResultStore>);
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
