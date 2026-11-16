use choscordb_driver_api::{DeferredReader, DriverError, ErrorKind, Handle, Result, ValueChunk};
use std::sync::Arc;

#[derive(Clone)]
pub(crate) struct Reader(Option<Arc<dyn DeferredReader>>);
impl Reader {
    pub fn new(reader: Arc<dyn DeferredReader>) -> Self {
        Self(Some(reader))
    }
    pub async fn read<T: Send + 'static>(
        self,
        handle: Handle,
        offset: u64,
        max_bytes: usize,
        lease: T,
    ) -> Result<(ValueChunk, T)> {
        tokio::task::spawn_blocking(move || {
            let reader = self;
            let result = reader
                .0
                .as_ref()
                .expect("reader ownership")
                .read_chunk(handle, offset, max_bytes);
            result.map(|chunk| (chunk, lease))
        })
        .await
        .map_err(|_| DriverError::new(ErrorKind::Internal, "Original value worker failed"))?
    }
}
struct SaveError(DriverError);
impl From<std::io::Error> for SaveError {
    fn from(error: std::io::Error) -> Self {
        Self(DriverError::new(ErrorKind::Io, error.to_string()))
    }
}
impl From<DriverError> for SaveError {
    fn from(error: DriverError) -> Self {
        Self(error)
    }
}
impl Reader {
    pub async fn save(
        self,
        handle: Handle,
        destination: std::path::PathBuf,
        max_bytes: usize,
        stopped: impl Fn() -> bool + Send + 'static,
    ) -> Result<u64> {
        tokio::task::spawn_blocking(move || {
            let reader = self.0.as_ref().expect("reader ownership");
            let mut written = 0;
            crate::document_io::write_atomically_with(&destination, |file| {
                loop {
                    if stopped() {
                        return Err(SaveError(DriverError::new(
                            ErrorKind::Cancelled,
                            "The result closed before the value was saved",
                        )));
                    }
                    let chunk = reader.read_chunk(handle, written, max_bytes)?;
                    if chunk.offset != written {
                        return Err(SaveError(DriverError::new(
                            ErrorKind::Internal,
                            "Original value chunk is out of order",
                        )));
                    }
                    std::io::Write::write_all(file, &chunk.bytes)?;
                    written += chunk.bytes.len() as u64;
                    if written >= chunk.total_bytes {
                        return Ok(());
                    }
                    if chunk.bytes.is_empty() {
                        return Err(SaveError(DriverError::new(
                            ErrorKind::Internal,
                            "Original value ended before its length",
                        )));
                    }
                }
            })
            .map(|()| written)
            .map_err(|SaveError(error)| error)
        })
        .await
        .map_err(|_| DriverError::new(ErrorKind::Internal, "Original value worker failed"))?
    }
}
impl Drop for Reader {
    fn drop(&mut self) {
        if let Some(reader) = self.0.take() {
            tokio::task::spawn_blocking(move || drop(reader));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use choscordb_driver_api::{DeferredKind, QueryId};
    struct Panics;
    impl DeferredReader for Panics {
        fn read_chunk(&self, _: Handle, _: u64, _: usize) -> Result<ValueChunk> {
            panic!("injected reader panic")
        }
    }
    #[tokio::test]
    async fn panic_returns_structured_error_and_releases_reservation() {
        let memory = crate::memory::Memory::new(Default::default()).unwrap();
        let id = QueryId {
            slot: 0,
            generation: 0,
        };
        let lease = memory.acquire(id, false).await;
        let result = Reader::new(Arc::new(Panics)).read(id, 0, 1, lease).await;
        assert!(matches!(result, Err(error) if error.kind == ErrorKind::Internal));
        assert_eq!(memory.usage().used_bytes, 0);
    }
    struct Blocking {
        started: std::sync::Mutex<Option<tokio::sync::oneshot::Sender<()>>>,
        proceed: std::sync::Mutex<std::sync::mpsc::Receiver<()>>,
    }
    impl DeferredReader for Blocking {
        fn read_chunk(&self, _: Handle, offset: u64, _: usize) -> Result<ValueChunk> {
            self.started
                .lock()
                .unwrap()
                .take()
                .unwrap()
                .send(())
                .unwrap();
            self.proceed.lock().unwrap().recv().unwrap();
            Ok(ValueChunk {
                bytes: vec![7],
                offset,
                total_bytes: 1,
                kind: DeferredKind::Binary,
            })
        }
    }
    #[tokio::test]
    async fn abandoned_waiter_keeps_worker_memory_reserved() {
        let memory = crate::memory::Memory::new(Default::default()).unwrap();
        let id = QueryId {
            slot: 0,
            generation: 0,
        };
        let lease = memory.acquire(id, false).await;
        let reserved = lease.reserved_bytes();
        let (started, started_rx) = tokio::sync::oneshot::channel();
        let (proceed, proceed_rx) = std::sync::mpsc::channel();
        let reader = Reader::new(Arc::new(Blocking {
            started: std::sync::Mutex::new(Some(started)),
            proceed: std::sync::Mutex::new(proceed_rx),
        }));
        let waiter = tokio::spawn(reader.read(id, 0, 1, lease));
        started_rx.await.unwrap();
        waiter.abort();
        assert!(matches!(waiter.await, Err(error) if error.is_cancelled()));
        assert_eq!(memory.usage().used_bytes, reserved);
        proceed.send(()).unwrap();
        tokio::time::timeout(std::time::Duration::from_secs(2), async {
            while memory.usage().used_bytes != 0 {
                tokio::task::yield_now().await;
            }
        })
        .await
        .unwrap();
    }
}
