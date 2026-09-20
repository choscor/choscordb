use choscordb_driver_api::*;
use std::collections::hash_map::RandomState;
use std::hash::BuildHasher;
use std::io::{Read, Seek, SeekFrom, Write};
use std::sync::{Arc, Mutex};
const HEADER: u64 = 32;
static GENERATION: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(1);
const MAGIC: u64 = 0x53514c53504f4f4c;
struct Storage {
    file: std::fs::File,
    length: u64,
}
struct Reader {
    storage: Mutex<Storage>,
    generation: u32,
    // Keyed authentication binds headers to their exact position. Payload bytes
    // cannot impersonate records, without an unbounded in-memory offset index.
    key: RandomState,
}
#[derive(Clone)]
pub struct Spool {
    reader: Arc<Reader>,
}
fn io(_: std::io::Error) -> DriverError {
    DriverError::new(ErrorKind::Io, "Large-value temporary storage failed")
}
fn stale() -> DriverError {
    DriverError::new(ErrorKind::StaleHandle, "Large value is no longer available")
}
impl Reader {
    fn header(&self, storage: &mut Storage, handle: Handle) -> Result<(u64, DeferredKind)> {
        let start = u64::from(handle.slot);
        if handle.generation != self.generation
            || start
                .checked_add(HEADER)
                .is_none_or(|end| end > storage.length)
        {
            return Err(stale());
        }
        storage.file.seek(SeekFrom::Start(start)).map_err(io)?;
        let mut header = [0; HEADER as usize];
        storage.file.read_exact(&mut header).map_err(io)?;
        let word = |index| u64::from_le_bytes(header[index..index + 8].try_into().unwrap());
        let (magic, length, tag, signature) = (word(0), word(8), word(16), word(24));
        if magic != MAGIC
            || tag > 1
            || signature != self.key.hash_one((self.generation, start, length, tag))
            || start
                .checked_add(HEADER)
                .and_then(|n| n.checked_add(length))
                .is_none_or(|end| end > storage.length)
        {
            return Err(stale());
        }
        Ok((
            length,
            if tag == 1 {
                DeferredKind::Text
            } else {
                DeferredKind::Binary
            },
        ))
    }
}
impl DeferredReader for Reader {
    fn read_chunk(&self, handle: Handle, offset: u64, max_bytes: usize) -> Result<ValueChunk> {
        if max_bytes == 0 || max_bytes > MAX_VALUE_CHUNK_BYTES {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "Invalid large-value chunk size",
            ));
        }
        let mut storage = self.storage.lock().map_err(|_| stale())?;
        let (total_bytes, kind) = self.header(&mut storage, handle)?;
        let remaining = total_bytes.checked_sub(offset).ok_or_else(|| {
            DriverError::new(ErrorKind::InvalidInput, "Large-value offset exceeds length")
        })?;
        let count = remaining.min(max_bytes as u64) as usize;
        storage
            .file
            .seek(SeekFrom::Start(u64::from(handle.slot) + HEADER + offset))
            .map_err(io)?;
        let mut bytes = vec![0; count];
        storage.file.read_exact(&mut bytes).map_err(io)?;
        Ok(ValueChunk {
            bytes,
            offset,
            total_bytes,
            kind,
        })
    }
}
impl Spool {
    pub fn new() -> Result<Self> {
        let generation = GENERATION
            .fetch_update(
                std::sync::atomic::Ordering::Relaxed,
                std::sync::atomic::Ordering::Relaxed,
                |n| n.checked_add(1),
            )
            .map_err(|_| {
                DriverError::new(
                    ErrorKind::ResourceLimit,
                    "MySQL deferred-value generations exhausted",
                )
            })?;
        Ok(Self {
            reader: Arc::new(Reader {
                storage: Mutex::new(Storage {
                    file: tempfile::tempfile().map_err(io)?,
                    length: 0,
                }),
                generation,
                key: RandomState::new(),
            }),
        })
    }
    pub fn reader(&self) -> Arc<dyn DeferredReader> {
        self.reader.clone()
    }
    pub fn store(&mut self, bytes: &[u8], text: bool) -> Result<Value> {
        let mut storage = self.reader.storage.lock().map_err(|_| stale())?;
        let next = storage
            .length
            .checked_add(HEADER)
            .and_then(|n| n.checked_add(bytes.len() as u64))
            .filter(|n| *n <= u32::MAX as u64)
            .ok_or_else(|| {
                DriverError::new(ErrorKind::ResourceLimit, "Large-value spool exceeds 4 GiB")
            })?;
        let handle = Handle {
            slot: storage.length as u32,
            generation: self.reader.generation,
        };
        let start = storage.length;
        let length = bytes.len() as u64;
        let tag = u64::from(text);
        let signature = self
            .reader
            .key
            .hash_one((self.reader.generation, start, length, tag));
        storage.file.seek(SeekFrom::Start(start)).map_err(io)?;
        for word in [MAGIC, length, tag, signature] {
            storage.file.write_all(&word.to_le_bytes()).map_err(io)?;
        }
        storage.file.write_all(bytes).map_err(io)?;
        storage.length = next;
        Ok(Value::Deferred {
            handle,
            byte_length: length,
            database_type: if text { "TEXT" } else { "BLOB" }.into(),
        })
    }
}
