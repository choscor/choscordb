use super::*;
use crate::{
    ExportId, ImportFile, ImportFlow, ImportRead, PlannedImport, TableImportRequest,
    TableImportTarget, count_import_rows, import_failure_row, plan_table_import, read_import_file,
    read_import_rows,
};
use choscordb_export::Cancellation;

fn stopped(cancellation: &Cancellation, shutdown: &watch::Receiver<bool>) -> Result<()> {
    if *shutdown.borrow() {
        return Err(DriverError::new(
            ErrorKind::Disconnected,
            "Import interrupted by disconnect",
        ));
    }
    if cancellation.is_cancelled() {
        return Err(DriverError::new(ErrorKind::Cancelled, "Import cancelled"));
    }
    Ok(())
}

const IMPORT_PROGRESS_INTERVAL: std::time::Duration = std::time::Duration::from_millis(250);
const IMPORT_CHUNK_ROWS: usize = 500;
const IMPORT_CHUNKS_IN_FLIGHT: usize = 2;

fn reader_stopped() -> DriverError {
    DriverError::new(ErrorKind::Internal, "The import file reader stopped")
}

fn file_changed() -> DriverError {
    DriverError::new(
        ErrorKind::InvalidInput,
        "The file changed during the import. Start the import again.",
    )
}

fn stream_rows(
    file: &ImportFile,
    plan: &PlannedImport,
    total: u64,
    cancellation: &Cancellation,
    sender: &tokio::sync::mpsc::Sender<Result<EditChunk>>,
) -> bool {
    let mut chunk = Vec::with_capacity(IMPORT_CHUNK_ROWS);
    let mut index = 0_usize;
    let mut delivered = true;
    let read = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        read_import_rows(file, &mut |row| {
            if cancellation.is_cancelled() {
                return Err(DriverError::new(ErrorKind::Cancelled, "Import cancelled"));
            }
            if u64::try_from(index).map_or(true, |rows| rows >= total) {
                return Err(file_changed());
            }
            chunk.push(plan.statement(&row)?);
            index += 1;
            if chunk.len() == IMPORT_CHUNK_ROWS {
                let rows = EditChunk::Rows(std::mem::take(&mut chunk));
                delivered = sender.blocking_send(Ok(rows)).is_ok();
                if !delivered {
                    return Ok(ImportFlow::Stop);
                }
            }
            Ok(ImportFlow::Continue)
        })
    }));
    if !delivered {
        return false;
    }
    let read = match read {
        Ok(Ok(_)) if u64::try_from(index) != Ok(total) => Err(file_changed()),
        Ok(Ok(_)) => Ok(()),
        Ok(Err(error)) => Err(error),
        Err(_) => Err(reader_stopped()),
    };
    match read {
        Ok(()) => sender.blocking_send(Ok(EditChunk::Rows(chunk))).is_ok(),
        Err(error) => {
            if let Err(rejected) = sender.blocking_send(Err(error)) {
                drop(rejected);
            }
            false
        }
    }
}

struct RowWriter {
    stream: EditStream,
    end: tokio::sync::mpsc::OwnedPermit<Result<EditChunk>>,
    producer: tokio::task::JoinHandle<bool>,
    total: u64,
}

async fn write_rows(
    connection: &mut dyn Connection,
    import: ExportId,
    writer: RowWriter,
    cancellation: &Cancellation,
    events: &EventSink,
) -> Result<u64> {
    let RowWriter {
        stream,
        end,
        mut producer,
        total,
    } = writer;
    let progress = stream.progress.clone();
    let report = |rows| Event::TableImportProgress {
        import,
        rows,
        total,
    };
    let handle = connection.cancellation_handle();
    let apply = connection.apply_edit_stream(stream);
    tokio::pin!(apply);
    let mut ticks = tokio::time::interval(IMPORT_PROGRESS_INTERVAL);
    ticks.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
    ticks.tick().await;
    let mut end = Some(end);
    let mut produced = false;
    let mut ended = false;
    let mut cancel_sent = false;
    let mut cancel_failure = None;
    let summary = loop {
        tokio::select! {
            biased;
            summary = &mut apply => break summary,
            _ = cancellation.cancelled(), if !cancel_sent => {
                cancel_sent = true;
                end = None;
                cancel_failure = handle.cancel().await.err();
            }
            delivered = &mut producer, if !produced => {
                produced = true;
                if let (Ok(true), Some(permit)) = (delivered, end.take()) {
                    permit.send(Ok(EditChunk::End));
                    ended = true;
                }
            }
            _ = ticks.tick() => send(events, report(progress.statements())).await,
        }
    };
    let reader = if produced {
        Ok(())
    } else {
        producer.await.map(|_| ())
    };
    let summary = match (summary, cancel_failure, reader) {
        (Err(error), Some(cancel), _) => Err(DriverError {
            message: format!("{} The cancel failed: {}", error.message, cancel.message),
            ..error
        }),
        (Err(error), None, _) => Err(error),
        (Ok(_), _, Err(_)) => Err(reader_stopped()),
        (Ok(_), _, Ok(())) if !ended => Err(reader_stopped()),
        (summary, _, Ok(())) => summary,
    }?;
    send(events, report(progress.statements())).await;
    Ok(summary.affected_rows.iter().sum())
}

async fn import(
    connection: &mut dyn Connection,
    import: ExportId,
    request: TableImportRequest,
    cancellation: &Cancellation,
    events: &EventSink,
    shutdown: &watch::Receiver<bool>,
) -> Result<u64> {
    stopped(cancellation, shutdown)?;
    let target = match &request.target {
        TableImportTarget::Existing { object } => {
            let mut shutdown = shutdown.clone();
            Some(metadata_operation(connection.inspect_edit_target(object), &mut shutdown).await?)
        }
        TableImportTarget::New { .. } => None,
    };
    let file = request.file.clone();
    let (plan, total) = tokio::task::spawn_blocking(move || {
        let preview = read_import_file(&request.file, ImportRead::Preview)?;
        let plan = plan_table_import(&request, target.as_ref(), &preview.columns)?;
        Ok::<_, DriverError>((plan, count_import_rows(&request.file)?))
    })
    .await
    .map_err(|_| reader_stopped())??;
    send(
        events,
        Event::TableImportProgress {
            import,
            rows: 0,
            total,
        },
    )
    .await;
    stopped(cancellation, shutdown)?;
    if let Some(create) = &plan.create {
        execute_ddl(connection, create).await?;
    }
    let drop = plan.drop.clone();
    let (sender, chunks) = tokio::sync::mpsc::channel(IMPORT_CHUNKS_IN_FLIGHT + 1);
    let end = sender
        .clone()
        .reserve_owned()
        .await
        .map_err(|_| reader_stopped())?;
    let producer_cancellation = cancellation.clone();
    let producer = tokio::task::spawn_blocking(move || {
        stream_rows(&file, &plan, total, &producer_cancellation, &sender)
    });
    let writer = RowWriter {
        stream: EditStream {
            chunks,
            progress: EditProgress::default(),
            cancel: cancellation.edit_cancel(),
        },
        end,
        producer,
        total,
    };
    let written = write_rows(connection, import, writer, cancellation, events).await;
    match (written, &drop) {
        (Err(error), Some(drop)) => match execute_ddl(connection, drop).await {
            Ok(()) => Err(error),
            Err(cleanup) => Err(DriverError {
                message: format!(
                    "{} The new table was not removed: {}",
                    error.message, cleanup.message
                ),
                ..error
            }),
        },
        (written, _) => written,
    }
}

pub(super) async fn run(
    connection: &mut dyn Connection,
    import_id: ExportId,
    request: TableImportRequest,
    cancellation: Cancellation,
    events: &EventSink,
    shutdown: &watch::Receiver<bool>,
) {
    let event = match import(
        connection,
        import_id,
        request,
        &cancellation,
        events,
        shutdown,
    )
    .await
    {
        Ok(rows) => Event::TableImportFinished {
            import: import_id,
            rows,
        },
        Err(error) => Event::TableImportFailed {
            import: import_id,
            row: import_failure_row(&error),
            error,
        },
    };
    send(events, event).await
}
