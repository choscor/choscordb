use choscordb_driver_api::*;
use std::collections::hash_map::RandomState;
use std::hash::BuildHasher;
use std::io::{Read, Seek, SeekFrom, Write};
use std::sync::{Arc, Mutex};
const HEADER: u64 = 32;
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
    pub fn new(generation: u32) -> Result<Self> {
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

#[cfg(test)]
mod tests {
    use super::*;
    fn handle(value: Value) -> Handle {
        let Value::Deferred { handle, .. } = value else {
            panic!("deferred")
        };
        handle
    }
    #[test]
    fn copied_header_inside_payload_is_not_a_record() {
        let mut spool = Spool::new(3).unwrap();
        let first = handle(spool.store(&[0; 64], false).unwrap());
        let mut storage = spool.reader.storage.lock().unwrap();
        let mut header = [0; HEADER as usize];
        storage.file.seek(SeekFrom::Start(0)).unwrap();
        storage.file.read_exact(&mut header).unwrap();
        storage.file.seek(SeekFrom::Start(HEADER)).unwrap();
        storage.file.write_all(&header).unwrap();
        drop(storage);
        assert!(
            spool
                .reader
                .read_chunk(
                    Handle {
                        slot: HEADER as u32,
                        ..first
                    },
                    0,
                    1
                )
                .is_err()
        );
        assert_eq!(spool.reader.read_chunk(first, 0, 1).unwrap().bytes.len(), 1);
    }
    #[test]
    fn rejects_corrupt_header_and_truncated_record() {
        for word in [0, 8, 16, 24] {
            let mut spool = Spool::new(4).unwrap();
            let first = handle(spool.store(b"hello", true).unwrap());
            let mut storage = spool.reader.storage.lock().unwrap();
            storage.file.seek(SeekFrom::Start(word)).unwrap();
            storage.file.write_all(&u64::MAX.to_le_bytes()).unwrap();
            drop(storage);
            assert!(spool.reader.read_chunk(first, 0, 1).is_err());
        }
        let mut spool = Spool::new(4).unwrap();
        let first = handle(spool.store(b"hello", true).unwrap());
        spool.reader.storage.lock().unwrap().length -= 1;
        assert!(spool.reader.read_chunk(first, 0, 1).is_err());
    }
    #[test]
    fn independent_reader_and_appends_share_file_position_safely() {
        let mut spool = Spool::new(5).unwrap();
        let first = handle(spool.store(&[42; 100000], false).unwrap());
        let reader = spool.reader();
        let writer = std::thread::spawn(move || {
            for _ in 0..100 {
                spool.store(&[17; 10000], false).unwrap();
            }
        });
        for _ in 0..100 {
            assert_eq!(
                reader.read_chunk(first, 99990, 20).unwrap().bytes,
                vec![42; 10]
            );
        }
        writer.join().unwrap();
        assert_eq!(reader.read_chunk(first, 0, 1).unwrap().bytes, vec![42]);
    }
}
