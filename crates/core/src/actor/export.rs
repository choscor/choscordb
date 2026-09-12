//! Original-result export is serialized with the connection actor. A pending read
//! owns its cursor, store and reservation until settlement, even when the exporter
//! drops its waiter on cancellation.
use super::*;
use choscordb_export::{Cancellation, ExportSource, FileSink, Limits};
use std::{collections::HashMap, path::PathBuf};
use tokio::task::JoinHandle;

struct State {
    store: crate::store::Store,
    active: Option<Active>,
    poisoned: bool,
}
type ReadResult = (State, Result<Option<ResultPage>>);
struct Source {
    columns: Vec<Column>,
    state: Option<State>,
    pending: Option<JoinHandle<ReadResult>>,
    reader: Option<crate::deferred::Reader>,
    lease: Arc<crate::PageLease>,
    query: QueryId,
    index: u64,
    done: bool,
    raw_limit: usize,
    cancellation: watch::Receiver<bool>,
    shutdown: watch::Receiver<bool>,
    events: mpsc::Sender<Event>,
    grace: Duration,
}
impl Source {
    async fn settle(&mut self) -> Result<Option<ResultPage>> {
        let Some(task) = self.pending.as_mut() else {
            return Ok(None);
        };
        let joined = task.await;
        self.pending = None;
        let (state, result) = joined
            .map_err(|_| DriverError::new(ErrorKind::Internal, "Export result worker failed"))?;
        self.state = Some(state);
        result
    }
}
#[async_trait::async_trait]
impl ExportSource for Source {
    fn columns(&self) -> &[Column] {
        &self.columns
    }
    async fn next_page(&mut self) -> Result<Option<ResultPage>> {
        if self.done {
            return Ok(None);
        }
        let mut state = self
            .state
            .take()
            .ok_or_else(|| DriverError::new(ErrorKind::Internal, "Export result unavailable"))?;
        let index = self.index;
        let lease = self.lease.clone();
        let query = self.query;
        let raw_limit = self.raw_limit;
        let mut cancellation = self.cancellation.clone();
        let mut shutdown = self.shutdown.clone();
        let events = self.events.clone();
        let grace = self.grace;
        self.pending = Some(tokio::spawn(async move {
            let result = async {
                let count = state.store.count()?;
                if index < count {
                    let (stored, _) = state.store.read_reserved(index, lease).await?;
                    return Ok(Some(stored.page));
                }
                let Some(current) = state.active.as_mut() else {
                    // An incomplete archived result must never silently export a prefix.
                    if count > 0 {
                        let (last, _) = state.store.read_reserved(count - 1, lease).await?;
                        if !last.page.has_more {
                            return Ok(None);
                        }
                    }
                    return Err(DriverError::new(
                        ErrorKind::StaleHandle,
                        "Original cursor closed before all rows were fetched",
                    ));
                };
                if current.completed {
                    return Ok(None);
                }
                let outcome = fetch_stored(
                    current,
                    &mut state.store,
                    lease,
                    current.page_size,
                    raw_limit,
                    Some(&mut cancellation),
                    &mut shutdown,
                    grace,
                    &events,
                )
                .await;
                state.poisoned |= outcome.poisoned;
                let (stored, _) = match outcome.result {
                    Ok(stored) => stored,
                    Err(error) => {
                        if let Some(current) = state.active.as_mut()
                            && let Some(history) = &mut current.history
                        {
                            history.fail(error.kind).await;
                        }
                        failure(&events, query, error.clone()).await;
                        if let Some(mut active) = state.active.take() {
                            let _ = active.cursor.close().await;
                        }
                        return Err(error);
                    }
                };
                if !stored.page.has_more {
                    complete_result(current, &events).await;
                }
                Ok(Some(stored.page))
            }
            .await;
            (state, result)
        }));
        let page = self.settle().await?;
        self.index += u64::from(page.is_some());
        self.done = page.as_ref().is_none_or(|page| !page.has_more);
        Ok(page)
    }
    async fn resolve(&mut self, _: Handle, _: usize) -> Result<Value> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Export requires bounded value chunks",
        ))
    }
    async fn read_value_chunk(
        &mut self,
        handle: Handle,
        offset: u64,
        max_bytes: usize,
    ) -> Result<ValueChunk> {
        let reader = self.reader.clone().ok_or_else(|| {
            DriverError::new(ErrorKind::StaleHandle, "Original value is unavailable")
        })?;
        let (chunk, _) = reader
            .read(
                handle,
                offset,
                max_bytes.min(self.raw_limit),
                self.lease.clone(),
            )
            .await?;
        Ok(chunk)
    }
}
#[allow(clippy::too_many_arguments)]
pub(super) async fn run(
    export: crate::ExportId,
    query: QueryId,
    destination: PathBuf,
    format: crate::ExportFormat,
    cancellation: Cancellation,
    mut released: watch::Receiver<bool>,
    stores: &mut HashMap<QueryId, crate::store::Store>,
    readers: &HashMap<QueryId, crate::deferred::Reader>,
    active: &mut Option<Active>,
    memory: &Arc<crate::memory::Memory>,
    events: &mpsc::Sender<Event>,
    shutdown: &watch::Receiver<bool>,
    grace: Duration,
) -> bool {
    let (stop_tx, stop) = watch::channel(false);
    let mut connection_shutdown = shutdown.clone();
    let cancel = cancellation.clone();
    let mut query_cancel = active
        .as_ref()
        .filter(|a| a.id == query && !a.completed)
        .map(|a| a.cancellation.clone());
    let monitor = tokio::spawn(async move {
        let query_stopped = async {
            if let Some(receiver) = query_cancel.as_mut() {
                super::super::operation::signalled(receiver).await;
            } else {
                std::future::pending::<()>().await;
            }
        };
        tokio::select! {
            _ = query_stopped => {},
            _ = cancel.cancelled() => {},
            _ = super::super::operation::signalled(&mut released) => {},
            _ = super::super::operation::signalled(&mut connection_shutdown) => {},
        }
        stop_tx.send_replace(true);
        cancel.cancel();
    });
    let mut stop_wait = stop.clone();
    let mut shutdown_wait = shutdown.clone();
    let lease = wait_capacity(
        memory.acquire(query, false),
        Some(&mut stop_wait),
        &mut shutdown_wait,
        None,
    )
    .await;
    let mut poisoned = false;
    let result = async {
        let lease = Arc::new(lease?);
        let mut store = stores.remove(&query).ok_or_else(|| {
            DriverError::new(ErrorKind::StaleHandle, "Original result is unavailable")
        })?;
        let schema = store.schema_reserved(lease.clone()).await;
        let columns = match schema {
            Ok((columns, _)) => columns,
            Err(error) => {
                stores.insert(query, store);
                return Err(error);
            }
        };
        let mut source = Source {
            columns,
            state: Some(State {
                store,
                active: active.take_if(|a| a.id == query),
                poisoned: false,
            }),
            pending: None,
            reader: readers.get(&query).cloned(),
            lease,
            query,
            index: 0,
            done: false,
            raw_limit: memory.raw_limit(),
            cancellation: stop,
            shutdown: shutdown.clone(),
            events: events.clone(),
            grace,
        };
        let (progress_tx, mut progress_rx) = mpsc::channel::<choscordb_export::Progress>(1);
        let notify = events.clone();
        let progress_task = tokio::spawn(async move {
            while let Some(progress) = progress_rx.recv().await {
                let _ = notify.try_send(Event::ExportProgress {
                    export,
                    query,
                    rows: progress.rows,
                    bytes: progress.bytes,
                });
            }
        });
        let result = match FileSink::create(destination).await {
            Ok(mut sink) => {
                choscordb_export::export(
                    &mut source,
                    &mut sink,
                    format,
                    cancellation,
                    Limits {
                        page_bytes: memory.raw_limit(),
                        value_bytes: memory.raw_limit(),
                        encoded_row_bytes: memory.raw_limit() * 4,
                    },
                    Some(progress_tx),
                )
                .await
            }
            Err(error) => Err(error),
        };
        // Cancellation of export() may have dropped next_page's waiter. Recover
        // ownership only after its native operation and file append have settled.
        let settled = source.settle().await;
        if let Some(state) = source.state.take() {
            stores.insert(query, state.store);
            if state.active.is_some() {
                *active = state.active;
            }
            poisoned = state.poisoned;
        } else {
            poisoned = true;
        }
        progress_task.abort();
        settled?;
        result
    }
    .await;
    monitor.abort();
    match result {
        Ok(progress) => {
            send(
                events,
                Event::ExportFinished {
                    export,
                    query,
                    rows: progress.rows,
                    bytes: progress.bytes,
                },
            )
            .await
        }
        Err(error) => {
            send(
                events,
                Event::ExportFailed {
                    export,
                    query,
                    error,
                },
            )
            .await
        }
    }
    poisoned
}
