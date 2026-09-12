use crate::{ExportFormat, format};
use async_trait::async_trait;
use choscordb_driver_api::*;
use std::{
    io::Write,
    path::PathBuf,
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
};
use tokio::sync::{Notify, mpsc, oneshot};
#[derive(Clone, Default)]
pub struct Cancellation {
    inner: Arc<CancelState>,
}
#[derive(Default)]
struct CancelState {
    cancelled: AtomicBool,
    notify: Notify,
}
impl Cancellation {
    pub fn cancel(&self) {
        self.inner.cancelled.store(true, Ordering::Release);
        self.inner.notify.notify_waiters();
    }
    pub fn is_cancelled(&self) -> bool {
        self.inner.cancelled.load(Ordering::Acquire)
    }
    pub async fn cancelled(&self) {
        loop {
            let notified = self.inner.notify.notified();
            tokio::pin!(notified);
            notified.as_mut().enable();
            if self.is_cancelled() {
                return;
            }
            notified.await;
        }
    }
}
fn cancelled() -> DriverError {
    DriverError::new(ErrorKind::Cancelled, "Export cancelled")
}
fn io_error() -> DriverError {
    DriverError::new(ErrorKind::Io, "Export destination write failed")
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Progress {
    pub rows: u64,
    pub bytes: u64,
}
#[derive(Clone, Copy, Debug)]
pub struct Limits {
    pub page_bytes: usize,
    /// Maximum inline value bytes or raw deferred chunk bytes, never total deferred length.
    pub value_bytes: usize,
    /// Conservative inline cell/schema allocation cap; all rows stream cell by cell.
    pub encoded_row_bytes: usize,
}
impl Default for Limits {
    fn default() -> Self {
        Self {
            page_bytes: 64 * 1024 * 1024,
            value_bytes: 64 * 1024 * 1024,
            encoded_row_bytes: 128 * 1024 * 1024,
        }
    }
}
/// Sources start at row zero of the original result. Reads must be cancellation-safe
/// and bound their own allocations; this service never submits SQL or reconnects.
#[async_trait]
pub trait ExportSource: Send {
    fn columns(&self) -> &[Column];
    async fn next_page(&mut self) -> Result<Option<ResultPage>>;
    async fn resolve(&mut self, _handle: Handle, _max_bytes: usize) -> Result<Value> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Whole value resolution unavailable",
        ))
    }
    /// Read raw bytes without allocating the complete value. Text chunks may split UTF-8.
    async fn read_value_chunk(
        &mut self,
        _handle: Handle,
        _offset: u64,
        _max_bytes: usize,
    ) -> Result<ValueChunk> {
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "Chunked value reads unavailable",
        ))
    }
}
/// Transactional sink seam. `finish` atomically publishes; `abort` discards partial
/// data. Implementations must keep the prior destination untouched before finish.
#[async_trait]
pub trait ExportSink: Send {
    async fn write(&mut self, bytes: &[u8]) -> Result<()>;
    async fn finish(&mut self) -> Result<()>;
    async fn abort(&mut self);
}
enum FileCommand {
    Write(Vec<u8>, oneshot::Sender<Result<()>>),
    Finish(oneshot::Sender<Result<()>>),
    Abort(oneshot::Sender<()>),
}
pub struct FileSink {
    commands: mpsc::Sender<FileCommand>,
}
impl FileSink {
    pub async fn create(destination: PathBuf) -> Result<Self> {
        let (tx, mut rx) = mpsc::channel(2);
        let (ready_tx, ready_rx) = oneshot::channel();
        std::thread::Builder::new()
            .name("choscordb-export".into())
            .spawn(move || {
                let parent = destination
                    .parent()
                    .filter(|p| !p.as_os_str().is_empty())
                    .unwrap_or_else(|| std::path::Path::new("."));
                let mut temporary = match tempfile::Builder::new()
                    .prefix(".choscordb-export-")
                    .tempfile_in(parent)
                {
                    Ok(f) => f,
                    Err(_) => {
                        let _ = ready_tx.send(Err(io_error()));
                        return;
                    }
                };
                if ready_tx.send(Ok(())).is_err() {
                    return;
                }
                while let Some(command) = rx.blocking_recv() {
                    match command {
                        FileCommand::Write(bytes, reply) => {
                            let _ = reply.send(temporary.write_all(&bytes).map_err(|_| io_error()));
                        }
                        FileCommand::Finish(reply) => {
                            let result = temporary
                                .as_file()
                                .sync_all()
                                .map_err(|_| io_error())
                                .and_then(|()| {
                                    temporary
                                        .persist(&destination)
                                        .map(|_| ())
                                        .map_err(|_| io_error())
                                });
                            let _ = reply.send(result);
                            return;
                        }
                        FileCommand::Abort(reply) => {
                            drop(temporary);
                            let _ = reply.send(());
                            return;
                        }
                    }
                }
            })
            .map_err(|_| io_error())?;
        ready_rx.await.map_err(|_| io_error())??;
        Ok(Self { commands: tx })
    }
}
#[async_trait]
impl ExportSink for FileSink {
    async fn write(&mut self, bytes: &[u8]) -> Result<()> {
        for chunk in bytes.chunks(64 * 1024) {
            let (tx, rx) = oneshot::channel();
            self.commands
                .send(FileCommand::Write(chunk.to_vec(), tx))
                .await
                .map_err(|_| io_error())?;
            rx.await.map_err(|_| io_error())??;
        }
        Ok(())
    }
    async fn finish(&mut self) -> Result<()> {
        let (tx, rx) = oneshot::channel();
        self.commands
            .send(FileCommand::Finish(tx))
            .await
            .map_err(|_| io_error())?;
        rx.await.map_err(|_| io_error())?
    }
    async fn abort(&mut self) {
        let (tx, rx) = oneshot::channel();
        if self.commands.send(FileCommand::Abort(tx)).await.is_ok() {
            let _ = rx.await;
        }
    }
}
pub async fn export(
    source: &mut dyn ExportSource,
    sink: &mut dyn ExportSink,
    format: ExportFormat,
    cancellation: Cancellation,
    limits: Limits,
    progress: Option<mpsc::Sender<Progress>>,
) -> Result<Progress> {
    let work = stream(source, sink, &format, &cancellation, limits, progress);
    let result =
        tokio::select! {biased; _=cancellation.cancelled()=>Err(cancelled()), result=work=>result};
    match result {
        Ok(p) => {
            if cancellation.is_cancelled() {
                sink.abort().await;
                return Err(cancelled());
            }
            match sink.finish().await {
                Ok(()) => Ok(p),
                Err(e) => {
                    sink.abort().await;
                    Err(e)
                }
            }
        }
        Err(e) => {
            sink.abort().await;
            Err(e)
        }
    }
}
async fn stream(
    source: &mut dyn ExportSource,
    sink: &mut dyn ExportSink,
    format: &ExportFormat,
    cancel: &Cancellation,
    limits: Limits,
    progress: Option<mpsc::Sender<Progress>>,
) -> Result<Progress> {
    crate::budget::schema(format, source.columns(), limits)?;
    let columns = source.columns().to_vec();
    let mut state = Progress::default();
    let header = format::header(format, &columns)?;
    sink.write(header.as_bytes()).await?;
    state.bytes += header.len() as u64;
    while let Some(page) = source.next_page().await? {
        if page.estimated_bytes() > limits.page_bytes {
            return Err(DriverError::new(
                ErrorKind::ResourceLimit,
                "Export page exceeds memory budget",
            ));
        }
        for row in &page.rows {
            if cancel.is_cancelled() {
                return Err(cancelled());
            }
            let bytes = crate::chunked::row(
                source,
                sink,
                format,
                &columns,
                row,
                state.rows == 0,
                crate::chunked::Config {
                    limits,
                    cancel,
                    progress: state,
                    sender: progress.as_ref(),
                },
            )
            .await?;
            state.rows += 1;
            state.bytes += bytes;
        }
        if let Some(sender) = &progress {
            let _ = sender.try_send(state);
        }
    }
    let footer = format::footer(format);
    sink.write(footer.as_bytes()).await?;
    state.bytes += footer.len() as u64;
    if let Some(sender) = &progress {
        let _ = sender.try_send(state);
    }
    Ok(state)
}
