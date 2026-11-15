use super::*;
use crate::{
    ExportId, MAX_SQL_FILE_ERRORS, SQL_FILE_PROGRESS_STATEMENTS, SQL_FILE_READ_BYTES, SqlFileMode,
    SqlFileRun, SqlFileSplitter,
};
use choscordb_export::Cancellation;
use tokio::io::AsyncReadExt;

#[derive(Default)]
struct Tally {
    statements: u64,
    failed: u64,
    bytes: u64,
    total_bytes: u64,
    errors: Vec<String>,
}

fn stopped(cancellation: &Cancellation, shutdown: &watch::Receiver<bool>) -> Result<()> {
    if *shutdown.borrow() {
        return Err(DriverError::new(
            ErrorKind::Disconnected,
            "The SQL file run stopped because the connection closed.",
        ));
    }
    if cancellation.is_cancelled() {
        return Err(DriverError::new(
            ErrorKind::Cancelled,
            "The SQL file run was cancelled.",
        ));
    }
    Ok(())
}

fn file_error(error: std::io::Error) -> DriverError {
    DriverError::new(
        ErrorKind::InvalidInput,
        format!("The SQL file cannot be read: {error}"),
    )
}

pub(super) async fn execute(
    connection: &mut dyn Connection,
    sql: &str,
    cancellation: &Cancellation,
) -> Result<()> {
    let handle = connection.cancellation_handle();
    let run = execute_ddl(connection, sql);
    tokio::pin!(run);
    tokio::select! {
        biased;
        result = &mut run => result,
        _ = cancellation.cancelled() => {
            handle.cancel().await?;
            run.await
        }
    }
}

async fn progress(events: &EventSink, run: ExportId, tally: &Tally) {
    send(
        events,
        Event::SqlFileProgress {
            run,
            statements: tally.statements,
            failed: tally.failed,
            bytes: tally.bytes,
            total_bytes: tally.total_bytes,
        },
    )
    .await
}

async fn run_statements(
    connection: &mut dyn Connection,
    run: ExportId,
    request: &SqlFileRun,
    cancellation: &Cancellation,
    events: &EventSink,
    shutdown: &watch::Receiver<bool>,
    tally: &mut Tally,
) -> Result<()> {
    let mut file = tokio::fs::File::open(&request.file)
        .await
        .map_err(file_error)?;
    tally.total_bytes = file.metadata().await.map_err(file_error)?.len();
    let mut splitter = SqlFileSplitter::new(request.mysql);
    let mut read_size = SQL_FILE_READ_BYTES;
    let mut buffer = Vec::new();
    loop {
        stopped(cancellation, shutdown)?;
        buffer.clear();
        let read = (&mut file)
            .take(read_size as u64)
            .read_to_end(&mut buffer)
            .await
            .map_err(file_error)?;
        tally.bytes += read as u64;
        let at_end = read == 0;
        let statements = splitter.push(&buffer, at_end).map_err(|error| {
            DriverError::new(
                ErrorKind::InvalidInput,
                format!("Line {}: {}", error.line, error.message),
            )
        })?;
        read_size = if statements.is_empty() && !at_end {
            read_size.saturating_mul(2)
        } else {
            SQL_FILE_READ_BYTES
        };
        for statement in statements {
            stopped(cancellation, shutdown)?;
            match execute(connection, &statement.sql, cancellation).await {
                Ok(()) => tally.statements += 1,
                Err(error) if error.kind == ErrorKind::Cancelled => return Err(error),
                Err(mut error) => {
                    error.message = format!("Line {}: {}", statement.line, error.message);
                    if request.mode != SqlFileMode::ContinueOnError {
                        return Err(error);
                    }
                    tally.failed += 1;
                    if tally.errors.len() < MAX_SQL_FILE_ERRORS {
                        tally.errors.push(error.message);
                    }
                }
            }
            if (tally.statements + tally.failed).is_multiple_of(SQL_FILE_PROGRESS_STATEMENTS) {
                progress(events, run, tally).await;
            }
        }
        progress(events, run, tally).await;
        if at_end {
            return Ok(());
        }
    }
}

async fn run_file(
    connection: &mut dyn Connection,
    run: ExportId,
    request: &SqlFileRun,
    cancellation: &Cancellation,
    events: &EventSink,
    shutdown: &watch::Receiver<bool>,
    tally: &mut Tally,
) -> Result<()> {
    stopped(cancellation, shutdown)?;
    if request.mode != SqlFileMode::Transaction {
        return run_statements(
            connection,
            run,
            request,
            cancellation,
            events,
            shutdown,
            tally,
        )
        .await;
    }
    execute_ddl(connection, "BEGIN").await?;
    match run_statements(
        connection,
        run,
        request,
        cancellation,
        events,
        shutdown,
        tally,
    )
    .await
    {
        Ok(()) => connection.commit().await,
        Err(error) => match connection.rollback().await {
            Ok(()) => Err(DriverError {
                message: format!("{} The transaction was rolled back.", error.message),
                ..error
            }),
            Err(rollback) => Err(DriverError {
                message: format!(
                    "{} The rollback also failed: {}",
                    error.message, rollback.message
                ),
                ..error
            }),
        },
    }
}

pub(super) async fn run(
    connection: &mut dyn Connection,
    run_id: ExportId,
    request: SqlFileRun,
    cancellation: Cancellation,
    events: &EventSink,
    shutdown: &watch::Receiver<bool>,
) {
    let mut tally = Tally::default();
    let outcome = run_file(
        connection,
        run_id,
        &request,
        &cancellation,
        events,
        shutdown,
        &mut tally,
    )
    .await;
    let event = match outcome {
        Ok(()) => Event::SqlFileFinished {
            run: run_id,
            statements: tally.statements,
            failed: tally.failed,
            errors: tally.errors,
        },
        Err(error) => Event::SqlFileFailed {
            run: run_id,
            statements: tally.statements,
            error,
        },
    };
    send(events, event).await
}
