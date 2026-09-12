//! Synchronous temporary storage for original result pages. Call only on a blocking
//! worker. Random reads use a fixed disk index; memory use does not grow with pages.
//!
//! Deferred values retain their original cursor handle. They cannot be resolved
//! after that cursor closes unless a caller separately copies their backing values.
//! This store never connects to a database or replays SQL. CRCs detect corruption;
//! they are not authentication. Files are ephemeral, with no crash recovery promise.
mod codec;
mod index;
use choscordb_driver_api::{Column, ResultPage};
use codec::{Decoder, Encoder};
pub use index::INDEX_RECORD_BYTES;
use index::Record;
use std::{
    fs::File,
    io::{BufReader, BufWriter, Read, Seek, SeekFrom, Write},
    path::Path,
};

#[derive(thiserror::Error)]
pub enum StoreError {
    #[error("result-store I/O failed")]
    Io(#[from] std::io::Error),
    #[error("result-store resource limit exceeded")]
    LimitExceeded,
    #[error("result-store data is corrupt")]
    Corrupt,
    #[error("result page is not stored")]
    PageMissing,
    #[error("page is not the next result page or has invalid dimensions")]
    InvalidPage,
    #[error("result-store append previously failed to roll back")]
    Poisoned,
    #[error("invalid result-store configuration")]
    InvalidConfiguration,
}
impl std::fmt::Debug for StoreError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Display::fmt(self, f)
    }
}
pub type Result<T> = std::result::Result<T, StoreError>;
#[derive(Clone, Debug)]
pub struct StoreConfig {
    /// Total schema, page data and index file bytes.
    pub max_disk_bytes: u64,
    pub max_page_encoded_bytes: u64,
    /// Owned allocation budget for one decoded page (excludes fixed I/O buffer).
    pub max_page_decoded_bytes: usize,
    pub max_schema_bytes: u64,
}
impl Default for StoreConfig {
    fn default() -> Self {
        Self {
            max_disk_bytes: 4 * 1024 * 1024 * 1024,
            max_page_encoded_bytes: 16 * 1024 * 1024,
            max_page_decoded_bytes: 64 * 1024 * 1024,
            max_schema_bytes: 4 * 1024 * 1024,
        }
    }
}
pub struct StoredPage {
    pub first_row: u64,
    pub page: ResultPage,
}
pub struct ResultStore {
    // Close file descriptors before TempDir removal, including on Windows.
    pages: File,
    index: File,
    schema_file: File,
    directory: tempfile::TempDir,
    config: StoreConfig,
    columns: u32,
    schema_bytes: u64,
    schema_checksum: u32,
    data_bytes: u64,
    count: u64,
    rows: u64,
    complete: bool,
    poisoned: bool,
}
impl ResultStore {
    pub fn new(columns: &[Column], config: StoreConfig) -> Result<Self> {
        Self::create(tempfile::tempdir()?, columns, config)
    }
    pub fn in_directory(
        parent: impl AsRef<Path>,
        columns: &[Column],
        config: StoreConfig,
    ) -> Result<Self> {
        Self::create(tempfile::tempdir_in(parent)?, columns, config)
    }
    fn create(
        directory: tempfile::TempDir,
        columns: &[Column],
        config: StoreConfig,
    ) -> Result<Self> {
        if config.max_disk_bytes == 0
            || config.max_page_encoded_bytes == 0
            || config.max_page_decoded_bytes == 0
            || config.max_schema_bytes == 0
            || columns.len() > 16_384
        {
            return Err(StoreError::InvalidConfiguration);
        }
        let open = |name| {
            std::fs::OpenOptions::new()
                .read(true)
                .write(true)
                .create_new(true)
                .open(directory.path().join(name))
        };
        let pages = open("pages")?;
        let index = open("index")?;
        let mut schema_file = open("schema")?;
        let mut writer = Encoder::new(
            BufWriter::new(&mut schema_file),
            config.max_schema_bytes.min(config.max_disk_bytes),
        );
        writer.schema(columns)?;
        let (schema_bytes, schema_checksum) = writer.finish()?;
        let mut store = Self {
            pages,
            index,
            schema_file,
            directory,
            config,
            columns: columns.len() as u32,
            schema_bytes,
            schema_checksum,
            data_bytes: 0,
            count: 0,
            rows: 0,
            complete: false,
            poisoned: false,
        };
        // Reject schemas that cannot later be decoded under the allocation budget.
        store.schema()?;
        Ok(store)
    }
    /// Directory is exposed for diagnostics and integrity tests, not persistence/reopening.
    pub fn directory(&self) -> &Path {
        self.directory.path()
    }
    pub fn page_count(&self) -> u64 {
        self.count
    }
    pub fn row_count(&self) -> u64 {
        self.rows
    }
    pub fn is_complete(&self) -> bool {
        self.complete
    }
    pub fn disk_bytes(&self) -> u64 {
        self.schema_bytes + self.data_bytes + self.count * INDEX_RECORD_BYTES as u64
    }
    pub fn schema(&mut self) -> Result<Vec<Column>> {
        self.schema_file.seek(SeekFrom::Start(0))?;
        let mut reader = Decoder::new(
            BufReader::new(&mut self.schema_file),
            self.schema_bytes,
            self.config.max_page_decoded_bytes,
        );
        let columns = reader.schema(self.columns)?;
        reader.finish(self.schema_checksum)?;
        Ok(columns)
    }
    /// Publishes the fixed index record only after the complete page has been written.
    /// Any write failure truncates unpublished bytes; a failed truncation poisons appends.
    pub fn append(&mut self, page: &ResultPage) -> Result<()> {
        if self.poisoned {
            return Err(StoreError::Poisoned);
        }
        if self.complete
            || page.index != self.count
            || page.rows.len() > 10_000
            || page
                .rows
                .iter()
                .any(|row| row.len() != self.columns as usize)
        {
            return Err(StoreError::InvalidPage);
        }
        if page.estimated_bytes() > self.config.max_page_decoded_bytes {
            return Err(StoreError::LimitExceeded);
        }
        let next_rows = self
            .rows
            .checked_add(page.rows.len() as u64)
            .ok_or(StoreError::LimitExceeded)?;
        let available = self
            .config
            .max_disk_bytes
            .checked_sub(self.disk_bytes())
            .and_then(|n| n.checked_sub(INDEX_RECORD_BYTES as u64))
            .ok_or(StoreError::LimitExceeded)?;
        let old_index = self
            .count
            .checked_mul(INDEX_RECORD_BYTES as u64)
            .ok_or(StoreError::LimitExceeded)?;
        let append = (|| -> Result<(u64, u32)> {
            self.pages.seek(SeekFrom::Start(self.data_bytes))?;
            let mut writer = Encoder::new(
                BufWriter::new(&mut self.pages),
                available.min(self.config.max_page_encoded_bytes),
            );
            writer.page(page, self.columns)?;
            let (length, checksum) = writer.finish()?;
            let record = Record {
                offset: self.data_bytes,
                length,
                first_row: self.rows,
                rows: page.rows.len() as u32,
                checksum,
            };
            self.index.seek(SeekFrom::Start(old_index))?;
            self.index.write_all(&record.encode())?;
            Ok((length, checksum))
        })();
        match append {
            Ok((length, _)) => {
                self.data_bytes += length;
                self.count += 1;
                self.rows = next_rows;
                self.complete = !page.has_more;
                Ok(())
            }
            Err(error) => {
                let data_rollback = self.pages.set_len(self.data_bytes);
                let index_rollback = self.index.set_len(old_index);
                if data_rollback.is_err() || index_rollback.is_err() {
                    self.poisoned = true;
                    return Err(StoreError::Poisoned);
                }
                Err(error)
            }
        }
    }
    pub fn read_page(&mut self, ordinal: u64) -> Result<StoredPage> {
        if ordinal >= self.count {
            return Err(StoreError::PageMissing);
        }
        self.index.seek(SeekFrom::Start(
            ordinal
                .checked_mul(INDEX_RECORD_BYTES as u64)
                .ok_or(StoreError::Corrupt)?,
        ))?;
        let mut bytes = [0; INDEX_RECORD_BYTES];
        self.index.read_exact(&mut bytes).map_err(|error| {
            if error.kind() == std::io::ErrorKind::UnexpectedEof {
                StoreError::Corrupt
            } else {
                error.into()
            }
        })?;
        let record = Record::decode(bytes)?;
        if record.length > self.config.max_page_encoded_bytes
            || record
                .offset
                .checked_add(record.length)
                .is_none_or(|end| end > self.data_bytes)
            || record
                .first_row
                .checked_add(u64::from(record.rows))
                .is_none_or(|end| end > self.rows)
        {
            return Err(StoreError::Corrupt);
        }
        self.pages.seek(SeekFrom::Start(record.offset))?;
        let mut reader = Decoder::new(
            BufReader::new(&mut self.pages),
            record.length,
            self.config.max_page_decoded_bytes,
        );
        let page = reader.page(ordinal, self.columns, record.rows)?;
        reader.finish(record.checksum)?;
        Ok(StoredPage {
            first_row: record.first_row,
            page,
        })
    }
    /// Explicit cleanup reports removal errors; Drop otherwise performs best-effort cleanup.
    pub fn close(self) -> Result<()> {
        let Self {
            pages,
            index,
            schema_file,
            directory,
            ..
        } = self;
        drop(pages);
        drop(index);
        drop(schema_file);
        directory.close()?;
        Ok(())
    }
}
