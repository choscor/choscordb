mod export;
use crate::{
    Event, QueryState,
    operation::{Control, perform, wait_capacity},
};
use choscordb_driver_api::*;
use std::{sync::Arc, time::Duration};
use tokio::{
    sync::{mpsc, watch},
    time::Instant,
};

pub(crate) enum Command {
    SqlMode {
        request_token: u64,
    },
    Edit {
        batch: EditBatch,
        request_token: u64,
    },
    EditTarget {
        object: ObjectId,
        request_token: u64,
    },
    EditQuery {
        sql: String,
        result_columns: Vec<String>,
        request_token: u64,
    },
    Export {
        export: crate::ExportId,
        query: QueryId,
        destination: std::path::PathBuf,
        format: crate::ExportFormat,
        cancellation: choscordb_export::Cancellation,
        released: watch::Receiver<bool>,
    },
    LoadValueChunk {
        query: QueryId,
        handle: Handle,
        offset: u64,
        max_bytes: usize,
        released: watch::Receiver<bool>,
    },
    LoadValue {
        query: QueryId,
        handle: Handle,
    },
    Ddl {
        object: ObjectId,
        request_token: u64,
    },
    Execute {
        query: QueryId,
        sql: Arc<String>,
        object: Option<ObjectId>,
        history: Option<crate::query_history::Ticket>,
        options: QueryOptions,
        cancellation: watch::Receiver<bool>,
    },
    NextResult {
        query: QueryId,
    },
    Fetch {
        query: QueryId,
        size: PageSize,
        index: Option<u64>,
        released: watch::Receiver<bool>,
    },
    Metadata {
        parent: Option<ObjectId>,
        request_token: u64,
        offset: u64,
        limit: u32,
    },
    Transaction(bool),
    Release(QueryId),
}
struct Active {
    page_size: PageSize,
    history: Option<crate::query_history::Ticket>,
    fetched_rows: u64,
    id: QueryId,
    _source_lease: crate::PageLease,
    completed: bool,
    cursor: Box<dyn ResultCursor>,
    cancellation: watch::Receiver<bool>,
    cancel: Arc<dyn CancelHandle>,
    started: Instant,
    deadline: Option<Instant>,
}
async fn finish_queued_history(commands: &mut mpsc::Receiver<Command>) {
    commands.close();
    while let Ok(command) = commands.try_recv() {
        if let Command::Execute { mut history, .. } = command
            && let Some(history) = &mut history
        {
            history.fail(ErrorKind::Disconnected).await;
        }
    }
}
async fn send(events: &mpsc::Sender<Event>, event: Event) {
    let _ = events.send(event).await;
}
async fn metadata_operation<T>(
    operation: impl std::future::Future<Output = Result<T>>,
    shutdown: &mut watch::Receiver<bool>,
) -> Result<T> {
    // Query cancellation handles target an execution generation, not metadata.
    // Drop the borrowed operation on disconnect so connection.close can perform
    // adapter-specific cancellation/rollback. Never retry the abandoned request.
    tokio::select! {
        biased;
        _ = crate::operation::signalled(shutdown) => Err(DriverError::new(
            ErrorKind::Disconnected,
            "Metadata operation interrupted by disconnect",
        )),
        result = operation => result,
    }
}
async fn failure(events: &mpsc::Sender<Event>, query: QueryId, error: DriverError) {
    let state = if error.kind == ErrorKind::Disconnected {
        QueryState::Disconnected
    } else {
        QueryState::Failed
    };
    send(events, Event::QueryState { query, state }).await;
    send(events, Event::QueryFailed { query, error }).await;
}
async fn close_active(active: &mut Option<Active>, events: &mpsc::Sender<Event>) {
    if let Some(mut old) = active.take() {
        match old.cursor.close().await {
            Ok(()) if !old.completed => {
                if let Some(history) = &mut old.history {
                    let rows = if old.cursor.columns().is_empty() {
                        old.cursor.summary().affected_rows
                    } else {
                        None
                    };
                    history.finish(crate::HistoryStatus::Completed, rows).await;
                }
                old.history.take();
                send(
                    events,
                    Event::QueryState {
                        query: old.id,
                        state: QueryState::Completed,
                    },
                )
                .await;
                send(
                    events,
                    Event::QueryFinished {
                        query: old.id,
                        duration: old.started.elapsed(),
                        summary: old.cursor.summary(),
                    },
                )
                .await;
            }
            Ok(()) => (),
            Err(error) => {
                if let Some(history) = &mut old.history {
                    history.fail(error.kind).await;
                }
                failure(events, old.id, error).await;
            }
        }
    }
}
/// Fetch and persist exactly once; grid paging and export share cursor advancement
/// and completion bookkeeping. The reservation follows blocking serialization.
#[allow(clippy::too_many_arguments)]
async fn fetch_stored<T: Send + 'static>(
    current: &mut Active,
    store: &mut crate::store::Store,
    lease: T,
    size: PageSize,
    raw_limit: usize,
    cancellation: Option<&mut watch::Receiver<bool>>,
    shutdown: &mut watch::Receiver<bool>,
    grace: Duration,
    events: &mpsc::Sender<Event>,
) -> crate::operation::Outcome<(choscordb_result_store::StoredPage, T)> {
    let outcome = perform(
        current.cursor.fetch_page_bounded(size, raw_limit),
        current.cancel.clone(),
        Control {
            query: current.id,
            cancellation: cancellation.unwrap_or(&mut current.cancellation),
            shutdown,
            deadline: current.deadline,
            grace,
            events,
        },
    )
    .await;
    let result = async move {
        let page = outcome.result?;
        current.fetched_rows = current.fetched_rows.saturating_add(page.rows.len() as u64);
        let stored = store.append_reserved(page, lease).await?;
        Ok(stored)
    }
    .await;
    crate::operation::Outcome {
        result,
        poisoned: outcome.poisoned,
    }
}
async fn complete_result(current: &mut Active, events: &mpsc::Sender<Event>) {
    if current.completed {
        return;
    }
    current.completed = true;
    if let Some(history) = &mut current.history {
        let rows = if current.cursor.columns().is_empty() {
            current.cursor.summary().affected_rows
        } else {
            Some(current.fetched_rows)
        };
        history.finish(crate::HistoryStatus::Completed, rows).await;
    }
    current.history.take();
    if let Some(retained) = current.cursor.retained_bytes_after_completion()
        && retained.saturating_add(256) <= current._source_lease.reserved_bytes()
    {
        current
            ._source_lease
            .shrink_to(retained)
            .expect("source reservation only shrinks");
    }
    send(
        events,
        Event::QueryState {
            query: current.id,
            state: QueryState::Completed,
        },
    )
    .await;
    send(
        events,
        Event::QueryFinished {
            query: current.id,
            duration: current.started.elapsed(),
            summary: current.cursor.summary(),
        },
    )
    .await;
}
#[allow(clippy::too_many_arguments)]
pub(crate) async fn run(
    id: ConnectionId,
    driver: Arc<dyn DatabaseDriver>,
    options: ConnectionOptions,
    mut commands: mpsc::Receiver<Command>,
    events: mpsc::Sender<Event>,
    mut shutdown: watch::Receiver<bool>,
    grace: Duration,
    mut store_config: choscordb_result_store::StoreConfig,
    store_directory: Option<std::path::PathBuf>,
    memory: Arc<crate::memory::Memory>,
    cache: Arc<crate::hot_cache::HotCache>,
) {
    let raw_limit = memory.raw_limit();
    store_config.max_page_decoded_bytes = store_config.max_page_decoded_bytes.min(raw_limit);
    store_config.max_schema_bytes = store_config.max_schema_bytes.min(raw_limit as u64);
    let stopped = async {
        if !*shutdown.borrow() {
            let _ = shutdown.changed().await;
        }
    };
    let connected = tokio::select! { biased; _ = stopped => {
        finish_queued_history(&mut commands).await;
        send(&events, Event::Disconnected { connection: id }).await;
        return;
    }, value = driver.connect(options) => value };
    let mut connection = match connected {
        Ok(connection) => connection,
        Err(error) => {
            finish_queued_history(&mut commands).await;
            send(
                &events,
                Event::ConnectionFailed {
                    connection: id,
                    error,
                },
            )
            .await;
            return;
        }
    };
    send(
        &events,
        Event::Connected {
            connection: id,
            capabilities: driver.capabilities(),
        },
    )
    .await;
    if let Ok(Some(mode)) = connection.sql_mode().await {
        send(
            &events,
            Event::SessionSqlMode {
                connection: id,
                mode,
                request_token: 0,
            },
        )
        .await;
    }
    let mut sql_active: Option<Active> = None;
    let mut object_active: Option<Active> = None;
    let mut readers = std::collections::HashMap::<QueryId, crate::deferred::Reader>::new();
    let mut stores = std::collections::HashMap::<QueryId, crate::store::Store>::new();
    loop {
        if *shutdown.borrow() {
            break;
        }
        let mut idle_cancellation = sql_active
            .as_ref()
            .filter(|a| !a.completed)
            .map(|a| a.cancellation.clone());
        let idle_deadline = sql_active
            .as_ref()
            .filter(|a| !a.completed)
            .and_then(|a| a.deadline);
        let idle_cancel = async {
            if let Some(receiver) = idle_cancellation.as_mut() {
                loop {
                    if *receiver.borrow_and_update() {
                        return;
                    }
                    if receiver.changed().await.is_err() {
                        std::future::pending::<()>().await;
                    }
                }
            }
            std::future::pending::<()>().await;
        };
        let idle_timeout = async {
            if let Some(deadline) = idle_deadline {
                tokio::time::sleep_until(deadline).await;
            } else {
                std::future::pending::<()>().await;
            }
        };
        let mut object_stop = object_active
            .as_ref()
            .filter(|a| !a.completed)
            .map(|a| a.cancellation.clone());
        let object_deadline = object_active
            .as_ref()
            .filter(|a| !a.completed)
            .and_then(|a| a.deadline);
        let object_cancelled = async {
            if let Some(receiver) = object_stop.as_mut() {
                crate::operation::signalled(receiver).await;
            } else {
                std::future::pending::<()>().await;
            }
        };
        let object_timed_out = async {
            if let Some(deadline) = object_deadline {
                tokio::time::sleep_until(deadline).await;
            } else {
                std::future::pending::<()>().await;
            }
        };
        let command = tokio::select! {
            biased;
            _ = shutdown.changed() => break,
            _ = idle_cancel => {
                if let Some(mut old) = sql_active.take() {
                    let settled = tokio::time::timeout(grace, old.cancel.cancel()).await;
                    send(&events, Event::QueryState { query: old.id, state: QueryState::Cancelling }).await;
                    let _ = old.cursor.close().await;
                    if let Some(history) = &mut old.history { history.fail(ErrorKind::Cancelled).await; }
                    failure(&events, old.id, DriverError::new(ErrorKind::Cancelled, "Query cancelled")).await;
                    if !matches!(settled, Ok(Ok(()))) { break; }
                }
                continue;
            }
            _ = idle_timeout => {
                if let Some(mut old) = sql_active.take() {
                    let settled = tokio::time::timeout(grace, old.cancel.cancel()).await;
                    send(&events, Event::QueryState { query: old.id, state: QueryState::Cancelling }).await;
                    let _ = old.cursor.close().await;
                    if let Some(history) = &mut old.history { history.fail(ErrorKind::Timeout).await; }
                    failure(&events, old.id, DriverError::new(ErrorKind::Timeout, "Query timed out")).await;
                    if !matches!(settled, Ok(Ok(()))) { break; }
                }
                continue;
            }
            _ = object_cancelled => {
                if let Some(mut old)=object_active.take() {
                    send(&events,Event::QueryState{query:old.id,state:QueryState::Cancelling}).await;
                    let _=old.cancel.cancel().await;let _=old.cursor.close().await;
                    failure(&events,old.id,DriverError::new(ErrorKind::Cancelled,"Object read cancelled")).await;
                }
                continue;
            }
            _ = object_timed_out => {
                if let Some(mut old)=object_active.take() {
                    let _=old.cancel.cancel().await;let _=old.cursor.close().await;
                    failure(&events,old.id,DriverError::new(ErrorKind::Timeout,"Object read timed out")).await;
                }
                continue;
            }
            command = commands.recv() => match command { Some(command) => command, None => break },
        };
        let object_command = match &command {
            Command::Execute {
                object: Some(_), ..
            } => true,
            Command::Fetch { query, .. }
            | Command::Export { query, .. }
            | Command::LoadValue { query, .. }
            | Command::LoadValueChunk { query, .. }
            | Command::Release(query) => object_active.as_ref().is_some_and(|a| a.id == *query),
            _ => false,
        };
        let active = if object_command {
            &mut object_active
        } else {
            &mut sql_active
        };
        match command {
            Command::SqlMode { request_token } => {
                // Inspect the session without retiring its current results. A
                // user may still reject the subsequent execution confirmation.
                let outcome = if let Some(current) = active.as_mut() {
                    perform(
                        connection.sql_mode(),
                        current.cancel.clone(),
                        Control {
                            query: current.id,
                            cancellation: &mut current.cancellation,
                            shutdown: &mut shutdown,
                            deadline: None,
                            grace,
                            events: &events,
                        },
                    )
                    .await
                } else {
                    crate::operation::Outcome {
                        result: metadata_operation(connection.sql_mode(), &mut shutdown).await,
                        poisoned: false,
                    }
                };
                match outcome.result {
                    Ok(mode) => {
                        send(
                            &events,
                            Event::SessionSqlMode {
                                connection: id,
                                mode: mode.unwrap_or_default(),
                                request_token,
                            },
                        )
                        .await
                    }
                    Err(error) => {
                        if let Some(mut old) = active.take() {
                            if let Some(history) = &mut old.history {
                                history.fail(error.kind).await;
                            }
                            let _ = old.cursor.close().await;
                            failure(&events, old.id, error.clone()).await;
                        }
                        send(
                            &events,
                            Event::OperationFailed {
                                connection: id,
                                error,
                            },
                        )
                        .await
                    }
                }
                if outcome.poisoned {
                    break;
                }
            }
            Command::EditQuery {
                sql,
                result_columns,
                request_token,
            } => {
                match metadata_operation(
                    connection.inspect_edit_query(&sql, result_columns),
                    &mut shutdown,
                )
                .await
                {
                    Ok(query) => {
                        send(
                            &events,
                            Event::EditQuery {
                                connection: id,
                                request_token,
                                query,
                            },
                        )
                        .await
                    }
                    Err(error) => {
                        send(
                            &events,
                            Event::EditQueryFailed {
                                connection: id,
                                request_token,
                                error,
                            },
                        )
                        .await
                    }
                }
            }
            Command::EditTarget {
                object,
                request_token,
            } => {
                match metadata_operation(connection.inspect_edit_target(&object), &mut shutdown)
                    .await
                {
                    Ok(target) => {
                        send(
                            &events,
                            Event::EditTarget {
                                connection: id,
                                request_token,
                                target,
                            },
                        )
                        .await
                    }
                    Err(error) => {
                        send(
                            &events,
                            Event::EditTargetFailed {
                                connection: id,
                                request_token,
                                error,
                            },
                        )
                        .await
                    }
                }
            }
            Command::Edit {
                batch,
                request_token,
            } => {
                match metadata_operation(connection.apply_edit_batch(batch), &mut shutdown).await {
                    Ok(summary) => {
                        send(
                            &events,
                            Event::EditApplied {
                                connection: id,
                                request_token,
                                summary,
                            },
                        )
                        .await
                    }
                    Err(error) => {
                        send(
                            &events,
                            Event::EditFailed {
                                connection: id,
                                request_token,
                                error,
                            },
                        )
                        .await
                    }
                }
            }
            Command::Execute {
                query,
                sql,
                object,
                mut history,
                options,
                mut cancellation,
            } => {
                close_active(active, &events).await;
                if *cancellation.borrow() {
                    if let Some(history) = &mut history {
                        history.fail(ErrorKind::Cancelled).await;
                    }
                    failure(
                        &events,
                        query,
                        DriverError::new(ErrorKind::Cancelled, "Queued query cancelled"),
                    )
                    .await;
                    continue;
                }
                let page_size = options.page_size;
                let started = Instant::now();
                let deadline = options
                    .timeout
                    .and_then(|timeout| started.checked_add(timeout));
                let source_lease = match wait_capacity(
                    memory.acquire(query, true),
                    Some(&mut cancellation),
                    &mut shutdown,
                    deadline,
                )
                .await
                {
                    Ok(lease) => lease,
                    Err(error) => {
                        if let Some(history) = &mut history {
                            history.fail(error.kind).await;
                        }
                        failure(&events, query, error).await;
                        continue;
                    }
                };
                let mut cancel = connection.cancellation_handle();
                send(
                    &events,
                    Event::QueryState {
                        query,
                        state: QueryState::Running,
                    },
                )
                .await;
                let outcome = if let Some(object) = object {
                    let result = metadata_operation(
                        connection.open_object(&object, raw_limit),
                        &mut shutdown,
                    )
                    .await;
                    if let Ok(cursor) = &result
                        && let Some(handle) = cursor.independent_cancellation_handle()
                    {
                        cancel = handle;
                    }
                    crate::operation::Outcome {
                        result,
                        poisoned: false,
                    }
                } else {
                    perform(
                        connection.execute_bounded(&sql, options, raw_limit),
                        cancel.clone(),
                        Control {
                            query,
                            cancellation: &mut cancellation,
                            shutdown: &mut shutdown,
                            deadline,
                            grace,
                            events: &events,
                        },
                    )
                    .await
                };
                match outcome.result {
                    Ok(mut cursor) => {
                        if let Some(reader) = cursor.deferred_reader() {
                            readers.insert(query, crate::deferred::Reader::new(reader));
                        }
                        if column_bytes(cursor.columns()) > raw_limit {
                            if let Some(history) = &mut history {
                                history.fail(ErrorKind::ResourceLimit).await;
                            }
                            let _ = cursor.close().await;
                            failure(
                                &events,
                                query,
                                DriverError::new(
                                    ErrorKind::ResourceLimit,
                                    "Result schema exceeds memory budget",
                                ),
                            )
                            .await;
                            continue;
                        }
                        let source_lease = match crate::store::Store::create_reserved(
                            cursor.columns().to_vec(),
                            store_config.clone(),
                            store_directory.clone(),
                            source_lease,
                        )
                        .await
                        {
                            Ok((store, source_lease)) => {
                                stores.insert(query, store);
                                source_lease
                            }
                            Err(error) => {
                                let _ = cursor.close().await;
                                if let Some(history) = &mut history {
                                    history.fail(error.kind).await;
                                }
                                failure(&events, query, error).await;
                                continue;
                            }
                        };

                        let mut schema_lease = match wait_capacity(
                            memory.acquire(query, false),
                            Some(&mut cancellation),
                            &mut shutdown,
                            deadline,
                        )
                        .await
                        {
                            Ok(lease) => lease,
                            Err(error) => {
                                let _ = cursor.close().await;
                                if let Some(history) = &mut history {
                                    history.fail(error.kind).await;
                                }
                                failure(&events, query, error).await;
                                continue;
                            }
                        };
                        // Schema payload remains pinned separately from pages in consumers.
                        schema_lease
                            .shrink_to(raw_limit * 4)
                            .expect("schema fits transfer reservation");
                        send(
                            &events,
                            Event::Schema {
                                query,
                                columns: cursor.columns().to_vec(),
                                lease: schema_lease,
                            },
                        )
                        .await;
                        *active = Some(Active {
                            page_size,
                            history,
                            fetched_rows: 0,
                            id: query,
                            _source_lease: source_lease,
                            completed: false,
                            cursor,
                            cancellation,
                            cancel,
                            started,
                            deadline,
                        });
                    }
                    Err(error) => {
                        if let Some(history) = &mut history {
                            history.fail(error.kind).await;
                        }
                        failure(&events, query, error).await;
                    }
                }
                if outcome.poisoned {
                    break;
                }
            }
            Command::NextResult { query } => {
                let Some(current) = active.as_mut().filter(|a| a.id == query) else {
                    failure(
                        &events,
                        query,
                        DriverError::new(ErrorKind::StaleHandle, "Result cursor is unavailable"),
                    )
                    .await;
                    continue;
                };
                // Navigation of completed result sets is not SQL execution. The
                // MySQL producer enforces its own execution deadline while active.
                if current.completed {
                    current.deadline = None;
                }
                let outcome = perform(
                    current.cursor.next_result_set(),
                    current.cancel.clone(),
                    Control {
                        query,
                        cancellation: &mut current.cancellation,
                        shutdown: &mut shutdown,
                        deadline: current.deadline,
                        grace,
                        events: &events,
                    },
                )
                .await;
                match outcome.result {
                    Ok(true) => {
                        let columns = current.cursor.columns().to_vec();
                        let result = async {
                            if column_bytes(&columns) > raw_limit {
                                return Err(DriverError::new(
                                    ErrorKind::ResourceLimit,
                                    "Result schema exceeds memory budget",
                                ));
                            }
                            let lease = wait_capacity(
                                memory.acquire(query, false),
                                Some(&mut current.cancellation),
                                &mut shutdown,
                                current.deadline,
                            )
                            .await?;
                            let (store, mut lease) = crate::store::Store::create_reserved(
                                columns.clone(),
                                store_config.clone(),
                                store_directory.clone(),
                                lease,
                            )
                            .await?;
                            stores.insert(query, store);
                            cache.remove(query);
                            current.completed = false;
                            current.fetched_rows = 0;
                            lease
                                .shrink_to(raw_limit * 4)
                                .expect("schema fits transfer reservation");
                            send(
                                &events,
                                Event::QueryState {
                                    query,
                                    state: QueryState::Running,
                                },
                            )
                            .await;
                            send(
                                &events,
                                Event::Schema {
                                    query,
                                    columns,
                                    lease,
                                },
                            )
                            .await;
                            Ok(())
                        }
                        .await;
                        if let Err(error) = result {
                            failure(&events, query, error).await;
                        }
                    }
                    Ok(false) => {
                        send(
                            &events,
                            Event::QueryFinished {
                                query,
                                duration: current.started.elapsed(),
                                summary: current.cursor.summary(),
                            },
                        )
                        .await
                    }
                    Err(error) => failure(&events, query, error).await,
                }
                if outcome.poisoned {
                    break;
                }
            }
            Command::Fetch {
                query,
                size,
                index,
                mut released,
            } => {
                let Some(store) = stores.get_mut(&query) else {
                    failure(
                        &events,
                        query,
                        DriverError::new(ErrorKind::StaleHandle, "Stored result is unavailable"),
                    )
                    .await;
                    continue;
                };
                let count = match store.count() {
                    Ok(count) => count,
                    Err(error) => {
                        send(&events, Event::QueryFailed { query, error }).await;
                        continue;
                    }
                };
                let live = active.as_mut().filter(|a| a.id == query && !a.completed);
                let deadline = live.as_ref().and_then(|a| a.deadline);
                // Archived pages survive execution cancellation. Only releasing
                // their query (or disconnecting) stops an archived capacity wait.
                let cancellation = live.map_or(&mut released, |a| &mut a.cancellation);
                let lease = match wait_capacity(
                    memory.acquire(query, false),
                    Some(cancellation),
                    &mut shutdown,
                    deadline,
                )
                .await
                {
                    Ok(lease) => lease,
                    Err(error) => {
                        if let Some(mut old) = active.take_if(|a| a.id == query && !a.completed) {
                            let settled = tokio::time::timeout(grace, old.cancel.cancel()).await;
                            let _ = old.cursor.close().await;
                            failure(&events, query, error).await;
                            if !matches!(settled, Ok(Ok(()))) {
                                break;
                            }
                        } else {
                            send(&events, Event::QueryFailed { query, error }).await;
                        }
                        continue;
                    }
                };
                if let Some(index) = index {
                    if index < count {
                        let read = if let Some(stored) = cache.read(query, index) {
                            Ok((stored, lease))
                        } else {
                            let read = store.read_reserved(index, lease).await;
                            if let Ok((stored, _)) = &read {
                                cache.insert(&memory, query, stored);
                            }
                            read
                        };
                        match read {
                            Ok((mut stored, lease)) => {
                                stored.page.has_more = index + 1 < count
                                    || (stored.page.has_more
                                        && active
                                            .as_ref()
                                            .is_some_and(|a| a.id == query && !a.completed));
                                send(
                                    &events,
                                    Event::StoredPage {
                                        query,
                                        first_row: stored.first_row,
                                        page: stored.page,
                                        lease,
                                    },
                                )
                                .await;
                            }
                            Err(error) => send(&events, Event::QueryFailed { query, error }).await,
                        }
                        continue;
                    }
                    if index > count {
                        send(
                            &events,
                            Event::QueryFailed {
                                query,
                                error: DriverError::new(
                                    ErrorKind::InvalidInput,
                                    "Page ordinal skips unfetched rows",
                                ),
                            },
                        )
                        .await;
                        continue;
                    }
                }

                let Some(current) = active.as_mut().filter(|a| a.id == query) else {
                    failure(
                        &events,
                        query,
                        DriverError::new(ErrorKind::StaleHandle, "Result cursor is closed"),
                    )
                    .await;
                    continue;
                };
                if current.completed {
                    send(
                        &events,
                        Event::QueryFailed {
                            query,
                            error: DriverError::new(
                                ErrorKind::InvalidInput,
                                "No page exists beyond the completed result",
                            ),
                        },
                    )
                    .await;
                    continue;
                }
                let outcome = fetch_stored(
                    current,
                    store,
                    lease,
                    size,
                    raw_limit,
                    None,
                    &mut shutdown,
                    grace,
                    &events,
                )
                .await;
                match outcome.result {
                    Ok((stored, lease)) => {
                        let finished = !stored.page.has_more;
                        cache.insert(&memory, query, &stored);
                        if index.is_some() {
                            send(
                                &events,
                                Event::StoredPage {
                                    query,
                                    first_row: stored.first_row,
                                    page: stored.page,
                                    lease,
                                },
                            )
                            .await;
                        } else {
                            send(
                                &events,
                                Event::Page {
                                    query,
                                    page: stored.page,
                                    lease,
                                },
                            )
                            .await;
                        }
                        if finished {
                            complete_result(current, &events).await;
                        }
                    }
                    Err(error) => {
                        if let Some(current) = active.as_mut()
                            && let Some(history) = &mut current.history
                        {
                            history.fail(error.kind).await;
                        }
                        failure(&events, query, error).await;
                        if let Some(mut old) = active.take() {
                            let _ = old.cursor.close().await;
                        }
                    }
                }
                if outcome.poisoned {
                    break;
                }
            }
            Command::Export {
                export,
                query,
                destination,
                format,
                cancellation,
                released,
            } => {
                let poisoned = export::run(
                    export,
                    query,
                    destination,
                    format,
                    cancellation,
                    released,
                    &mut stores,
                    &readers,
                    active,
                    &memory,
                    &events,
                    &shutdown,
                    grace,
                )
                .await;
                if poisoned {
                    break;
                }
            }
            Command::LoadValueChunk {
                query,
                handle,
                offset,
                max_bytes,
                mut released,
            } => {
                let result = async {
                    if !(1..=MAX_VALUE_CHUNK_BYTES).contains(&max_bytes) {
                        return Err(DriverError::new(
                            ErrorKind::ResourceLimit,
                            "Invalid value chunk size",
                        ));
                    }
                    let reader = readers
                        .get(&query)
                        .ok_or_else(|| {
                            DriverError::new(
                                ErrorKind::StaleHandle,
                                "Original value reader is unavailable",
                            )
                        })?
                        .clone();
                    let lease = wait_capacity(
                        memory.acquire(query, false),
                        Some(&mut released),
                        &mut shutdown,
                        None,
                    )
                    .await?;
                    reader
                        .read(handle, offset, max_bytes.min(memory.raw_limit()), lease)
                        .await
                }
                .await;
                match result {
                    Ok((chunk, lease)) => {
                        send(
                            &events,
                            Event::ValueChunk {
                                query,
                                handle,
                                chunk,
                                lease,
                            },
                        )
                        .await
                    }
                    Err(error) => {
                        send(
                            &events,
                            Event::ValueChunkFailed {
                                query,
                                handle,
                                offset,
                                error,
                            },
                        )
                        .await
                    }
                }
            }
            Command::LoadValue { query, handle } => {
                if let Some(current) = active.as_mut().filter(|a| a.id == query) {
                    match current.cursor.load_value(handle).await {
                        Ok(value) => {
                            send(
                                &events,
                                Event::Value {
                                    query,
                                    handle,
                                    value,
                                },
                            )
                            .await
                        }
                        Err(error) => send(&events, Event::QueryFailed { query, error }).await,
                    }
                } else {
                    send(
                        &events,
                        Event::QueryFailed {
                            query,
                            error: DriverError::new(
                                ErrorKind::StaleHandle,
                                "Result cursor is closed",
                            ),
                        },
                    )
                    .await;
                }
            }
            Command::Ddl {
                object,
                request_token,
            } => match metadata_operation(connection.object_ddl(&object), &mut shutdown).await {
                Ok(ddl) => {
                    send(
                        &events,
                        Event::Ddl {
                            connection: id,
                            object,
                            request_token,
                            ddl,
                        },
                    )
                    .await
                }
                Err(error) => {
                    send(
                        &events,
                        if request_token == 0 {
                            Event::OperationFailed {
                                connection: id,
                                error,
                            }
                        } else {
                            Event::DdlFailed {
                                connection: id,
                                object,
                                request_token,
                                error,
                            }
                        },
                    )
                    .await
                }
            },
            Command::Metadata {
                parent,
                request_token,
                offset,
                limit,
            } => match metadata_operation(
                connection.load_metadata_page(parent.clone(), offset, limit),
                &mut shutdown,
            )
            .await
            {
                Ok(objects) => {
                    send(
                        &events,
                        Event::Metadata {
                            connection: id,
                            parent,
                            request_token,
                            objects: objects.objects,
                            offset,
                            next_offset: objects.next_offset,
                        },
                    )
                    .await
                }
                Err(error) => {
                    send(
                        &events,
                        Event::MetadataFailed {
                            connection: id,
                            parent,
                            request_token,
                            error,
                        },
                    )
                    .await
                }
            },
            Command::Transaction(committed) => {
                close_active(active, &events).await;
                let result = if committed {
                    connection.commit().await
                } else {
                    connection.rollback().await
                };
                match result {
                    Ok(()) => {
                        send(
                            &events,
                            Event::TransactionFinished {
                                connection: id,
                                committed,
                            },
                        )
                        .await
                    }
                    Err(error) => {
                        send(
                            &events,
                            Event::OperationFailed {
                                connection: id,
                                error,
                            },
                        )
                        .await
                    }
                }
            }
            Command::Release(query) => {
                stores.remove(&query);
                readers.remove(&query);
                cache.remove(query);
                if active.as_ref().is_some_and(|a| a.id == query) {
                    close_active(active, &events).await;
                }
            }
        }
    }
    commands.close();
    if let Some(old) = sql_active.as_ref() {
        let _ = tokio::time::timeout(grace, old.cancel.cancel()).await;
    }
    // Cursor close may commit an automatic transaction (e.g. a suspended
    // PostgreSQL RETURNING portal). Disconnect must roll back the connection
    // before any cursor finalizer can run, even if native cancellation failed.
    let _ = connection.close().await;
    if let Some(mut old) = sql_active.take() {
        // The connection is closed; a cursor finalizer cannot make progress if
        // its worker has already stopped. Dropping it releases local resources.
        if let Some(history) = &mut old.history {
            history.fail(ErrorKind::Disconnected).await;
        }
        old.history.take();
        failure(
            &events,
            old.id,
            DriverError::new(ErrorKind::Disconnected, "Connection closed"),
        )
        .await;
    }
    if let Some(old) = object_active.take() {
        failure(
            &events,
            old.id,
            DriverError::new(ErrorKind::Disconnected, "Connection closed"),
        )
        .await;
    }
    while let Ok(command) = commands.try_recv() {
        if let Command::Execute {
            query, mut history, ..
        } = command
        {
            if let Some(history) = &mut history {
                history.fail(ErrorKind::Disconnected).await;
            }
            failure(
                &events,
                query,
                DriverError::new(ErrorKind::Disconnected, "Connection closed"),
            )
            .await;
        }
    }
    for query in stores.keys() {
        cache.remove(*query);
    }
    send(&events, Event::Disconnected { connection: id }).await;
}

fn column_bytes(columns: &[Column]) -> usize {
    columns
        .iter()
        .fold(std::mem::size_of::<Vec<Column>>(), |total, column| {
            total
                .saturating_add(std::mem::size_of::<Column>())
                .saturating_add(column.name.capacity())
                .saturating_add(column.database_type.capacity())
                .saturating_add(column.timezone.as_ref().map_or(0, String::capacity))
        })
}
