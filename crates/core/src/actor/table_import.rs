use super::*;
use crate::{
    ExportId, ImportRead, TableImportRequest, TableImportTarget, import_failure_row,
    plan_table_import, read_import_file,
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

async fn write_rows(
    connection: &mut dyn Connection,
    import: ExportId,
    batch: EditBatch,
    cancellation: &Cancellation,
    events: &EventSink,
) -> Result<u64> {
    let total = batch.statements.len() as u64;
    let progress = batch.progress.clone();
    let report = |rows| Event::TableImportProgress {
        import,
        rows,
        total,
    };
    let handle = connection.cancellation_handle();
    let apply = connection.apply_edit_batch(batch);
    tokio::pin!(apply);
    let mut ticks = tokio::time::interval(IMPORT_PROGRESS_INTERVAL);
    ticks.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
    ticks.tick().await;
    let summary = loop {
        tokio::select! {
            biased;
            summary = &mut apply => break summary,
            _ = cancellation.cancelled() => {
                handle.cancel().await?;
                break apply.await;
            }
            _ = ticks.tick() => send(events, report(progress.statements())).await,
        }
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
    let file = request.file.clone();
    let data = tokio::task::spawn_blocking(move || read_import_file(&file, ImportRead::All))
        .await
        .map_err(|_| DriverError::new(ErrorKind::Internal, "The import file reader stopped"))??;
    send(
        events,
        Event::TableImportProgress {
            import,
            rows: 0,
            total: data.rows.len() as u64,
        },
    )
    .await;
    let target = match &request.target {
        TableImportTarget::Existing { object } => {
            let mut shutdown = shutdown.clone();
            Some(metadata_operation(connection.inspect_edit_target(object), &mut shutdown).await?)
        }
        TableImportTarget::New { .. } => None,
    };
    let plan = plan_table_import(&request, target.as_ref(), data)?;
    stopped(cancellation, shutdown)?;
    if let Some(create) = &plan.create {
        execute_ddl(connection, create).await?;
    }
    let written = write_rows(connection, import, plan.batch, cancellation, events).await;
    match (written, &plan.drop) {
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
