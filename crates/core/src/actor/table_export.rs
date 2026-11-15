use super::*;
use crate::{ExportId, TableExportTarget};
use choscordb_export::{Cancellation, ExportSink, ExportSource, FileSink, Limits};
use std::path::PathBuf;

struct CursorSource {
    cursor: Box<dyn ResultCursor>,
    reader: Option<Arc<dyn DeferredReader>>,
    page_size: PageSize,
    page_bytes: usize,
    done: bool,
}

#[async_trait::async_trait]
impl ExportSource for CursorSource {
    fn columns(&self) -> &[Column] {
        self.cursor.columns()
    }
    async fn next_page(&mut self) -> Result<Option<ResultPage>> {
        if self.done {
            return Ok(None);
        }
        let page = self
            .cursor
            .fetch_page_bounded(self.page_size, self.page_bytes)
            .await?;
        self.done = !page.has_more;
        Ok(Some(page))
    }
    async fn read_value_chunk(
        &mut self,
        handle: Handle,
        offset: u64,
        max_bytes: usize,
    ) -> Result<ValueChunk> {
        self.reader
            .as_ref()
            .ok_or_else(|| {
                DriverError::new(ErrorKind::StaleHandle, "Original value is unavailable")
            })?
            .read_chunk(handle, offset, max_bytes)
    }
}

struct Segment<'a>(&'a mut FileSink);

#[async_trait::async_trait]
impl ExportSink for Segment<'_> {
    async fn write(&mut self, bytes: &[u8]) -> Result<()> {
        self.0.write(bytes).await
    }
    async fn finish(&mut self) -> Result<()> {
        Ok(())
    }
    async fn abort(&mut self) {}
}

fn stopped(cancellation: &Cancellation, shutdown: &watch::Receiver<bool>) -> Result<()> {
    if *shutdown.borrow() {
        return Err(DriverError::new(
            ErrorKind::Disconnected,
            "Export interrupted by disconnect",
        ));
    }
    if cancellation.is_cancelled() {
        return Err(DriverError::new(ErrorKind::Cancelled, "Export cancelled"));
    }
    Ok(())
}

async fn export_table(
    connection: &mut dyn Connection,
    sink: &mut FileSink,
    target: &TableExportTarget,
    cancellation: &Cancellation,
    shutdown: &watch::Receiver<bool>,
) -> Result<u64> {
    if let Some(drop) = &target.drop {
        sink.write(format!("{drop}\n").as_bytes()).await?;
    }
    if target.structure {
        let mut shutdown = shutdown.clone();
        let ddl = metadata_operation(connection.object_ddl(&target.object), &mut shutdown).await?;
        let ddl = ddl.trim_end();
        let end = if ddl.ends_with(';') { "" } else { ";" };
        sink.write(format!("{ddl}{end}\n").as_bytes()).await?;
    }
    let Some(data) = &target.data else {
        return Ok(0);
    };
    stopped(cancellation, shutdown)?;
    let cursor = connection
        .execute(&data.select, QueryOptions::default())
        .await?;
    let limits = Limits::default();
    let mut source = CursorSource {
        reader: cursor.deferred_reader(),
        cursor,
        page_size: PageSize::default(),
        page_bytes: limits.page_bytes,
        done: false,
    };
    let report = choscordb_export::export(
        &mut source,
        &mut Segment(sink),
        data.format.clone(),
        cancellation.clone(),
        limits,
        None,
    )
    .await;
    let closed = source.cursor.close().await;
    let report = report?;
    closed?;
    Ok(report.rows)
}

pub(super) async fn run(
    connection: &mut dyn Connection,
    export: ExportId,
    targets: Vec<TableExportTarget>,
    cancellation: Cancellation,
    events: &EventSink,
    shutdown: &watch::Receiver<bool>,
) {
    let total = targets.len() as u64;
    let mut rows = 0;
    let mut open: Option<(PathBuf, FileSink)> = None;
    for (index, target) in targets.iter().enumerate() {
        let result = async {
            stopped(&cancellation, shutdown)?;
            if open
                .as_ref()
                .is_some_and(|(path, _)| *path != target.destination)
                && let Some((_, mut sink)) = open.take()
            {
                sink.finish().await?;
            }
            if open.is_none() {
                open = Some((
                    target.destination.clone(),
                    FileSink::create(target.destination.clone()).await?,
                ));
            }
            let (_, sink) = open.as_mut().ok_or_else(|| {
                DriverError::new(ErrorKind::Internal, "Export destination is unavailable")
            })?;
            export_table(connection, sink, target, &cancellation, shutdown).await
        }
        .await;
        match result {
            Ok(count) => {
                rows += count;
                send(
                    events,
                    Event::TableExportProgress {
                        export,
                        completed: index as u64 + 1,
                        total,
                        rows,
                    },
                )
                .await;
            }
            Err(error) => {
                if let Some((_, mut sink)) = open.take() {
                    sink.abort().await;
                }
                send(
                    events,
                    Event::TableExportFailed {
                        export,
                        table: target.label.clone(),
                        error,
                    },
                )
                .await;
                return;
            }
        }
    }
    if let Some((_, mut sink)) = open.take()
        && let Err(error) = sink.finish().await
    {
        send(
            events,
            Event::TableExportFailed {
                export,
                table: String::new(),
                error,
            },
        )
        .await;
        return;
    }
    send(
        events,
        Event::TableExportFinished {
            export,
            tables: total,
            rows,
        },
    )
    .await;
}
