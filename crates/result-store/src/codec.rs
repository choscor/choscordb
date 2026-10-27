use crate::{Result, StoreError};
use choscordb_driver_api::*;
use std::io::{Read, Write};

pub(crate) struct Encoder<W> {
    out: W,
    limit: u64,
    pub bytes: u64,
    hash: crc32fast::Hasher,
}
impl<W: Write> Encoder<W> {
    pub fn new(out: W, limit: u64) -> Self {
        Self {
            out,
            limit,
            bytes: 0,
            hash: crc32fast::Hasher::new(),
        }
    }
    fn raw(&mut self, bytes: &[u8]) -> Result<()> {
        let total = self
            .bytes
            .checked_add(bytes.len() as u64)
            .filter(|n| *n <= self.limit)
            .ok_or(StoreError::LimitExceeded)?;
        self.out.write_all(bytes)?;
        self.hash.update(bytes);
        self.bytes = total;
        Ok(())
    }
    fn text(&mut self, text: &str) -> Result<()> {
        self.bytes(text.as_bytes())
    }
    fn optional<T>(
        &mut self,
        item: Option<T>,
        write: impl FnOnce(&mut Self, T) -> Result<()>,
    ) -> Result<()> {
        self.byte(u8::from(item.is_some()))?;
        if let Some(item) = item {
            write(self, item)?;
        }
        Ok(())
    }
    pub fn schema(&mut self, columns: &[Column]) -> Result<()> {
        self.u32(u32::try_from(columns.len()).map_err(|_| StoreError::LimitExceeded)?)?;
        for c in columns {
            self.text(&c.name)?;
            self.text(&c.database_type)?;
            self.optional(c.precision, Self::u32)?;
            self.optional(c.scale, |w, n| w.u32(n as u32))?;
            self.optional(c.timezone.as_deref(), Self::text)?;
            self.byte(match c.nullable {
                None => 0,
                Some(false) => 1,
                Some(true) => 2,
            })?;
        }
        Ok(())
    }
    pub fn page(&mut self, page: &ResultPage, columns: u32) -> Result<()> {
        self.u64(page.index)?;
        self.byte(u8::from(page.has_more))?;
        self.u32(page.rows.len() as u32)?;
        self.u32(columns)?;
        for row in &page.rows {
            for value in row {
                write_value(self, value)?;
            }
        }
        Ok(())
    }
    pub fn finish(mut self) -> Result<(u64, u32)> {
        self.out.flush()?;
        Ok((self.bytes, self.hash.finalize()))
    }
}

pub(crate) struct Decoder<R> {
    input: R,
    remaining: u64,
    budget: usize,
    hash: crc32fast::Hasher,
}
impl<R: Read> Decoder<R> {
    pub fn new(input: R, length: u64, budget: usize) -> Self {
        Self {
            input,
            remaining: length,
            budget,
            hash: crc32fast::Hasher::new(),
        }
    }
    fn raw(&mut self, bytes: &mut [u8]) -> Result<()> {
        if bytes.len() as u64 > self.remaining {
            return Err(StoreError::Corrupt);
        }
        self.input.read_exact(bytes).map_err(|error| {
            if error.kind() == std::io::ErrorKind::UnexpectedEof {
                StoreError::Corrupt
            } else {
                error.into()
            }
        })?;
        self.remaining -= bytes.len() as u64;
        self.hash.update(bytes);
        Ok(())
    }
    fn bool(&mut self) -> Result<bool> {
        match self.byte()? {
            0 => Ok(false),
            1 => Ok(true),
            _ => Err(StoreError::Corrupt),
        }
    }
    fn allocate(&mut self, bytes: usize) -> Result<()> {
        self.budget = self
            .budget
            .checked_sub(bytes)
            .ok_or(StoreError::LimitExceeded)?;
        Ok(())
    }
    fn text(&mut self) -> Result<String> {
        String::from_utf8(self.bytes()?).map_err(|_| StoreError::Corrupt)
    }
    fn optional<T>(&mut self, read: impl FnOnce(&mut Self) -> Result<T>) -> Result<Option<T>> {
        if self.bool()? {
            Ok(Some(read(self)?))
        } else {
            Ok(None)
        }
    }
    pub fn schema(&mut self, expected: u32) -> Result<Vec<Column>> {
        let count = self.u32()?;
        if count != expected {
            return Err(StoreError::Corrupt);
        }
        self.allocate(count as usize * std::mem::size_of::<Column>())?;
        let mut columns = Vec::with_capacity(count as usize);
        for _ in 0..count {
            columns.push(Column {
                name: self.text()?,
                database_type: self.text()?,
                precision: self.optional(Self::u32)?,
                scale: self.optional(|r| Ok(r.u32()? as i32))?,
                timezone: self.optional(Self::text)?,
                nullable: match self.byte()? {
                    0 => None,
                    1 => Some(false),
                    2 => Some(true),
                    _ => return Err(StoreError::Corrupt),
                },
            });
        }
        Ok(columns)
    }
    pub fn page(&mut self, expected: u64, columns: u32, expected_rows: u32) -> Result<ResultPage> {
        let index = self.u64()?;
        let has_more = self.bool()?;
        let rows = self.u32()?;
        let width = self.u32()?;
        if index != expected || rows != expected_rows || rows > 10_000 || width != columns {
            return Err(StoreError::Corrupt);
        }
        let values = (rows as usize)
            .checked_mul(width as usize)
            .ok_or(StoreError::Corrupt)?;
        if values as u64 > self.remaining {
            return Err(StoreError::Corrupt);
        }
        let arrays = (rows as usize)
            .checked_mul(std::mem::size_of::<Row>())
            .and_then(|n| n.checked_add(std::mem::size_of::<ResultPage>()))
            .ok_or(StoreError::LimitExceeded)?;
        self.allocate(arrays)?;
        self.allocate(
            values
                .checked_mul(std::mem::size_of::<Value>())
                .ok_or(StoreError::LimitExceeded)?,
        )?;
        let mut output = Vec::with_capacity(rows as usize);
        for _ in 0..rows {
            let mut row = Vec::with_capacity(width as usize);
            for _ in 0..width {
                row.push(read_value(self)?);
            }
            output.push(row);
        }
        Ok(ResultPage {
            index,
            rows: output,
            has_more,
        })
    }
    pub fn finish(self, checksum: u32) -> Result<()> {
        if self.remaining != 0 || self.hash.finalize() != checksum {
            Err(StoreError::Corrupt)
        } else {
            Ok(())
        }
    }
}

impl<W: Write> ValueWriter for Encoder<W> {
    type Error = StoreError;
    fn byte(&mut self, n: u8) -> Result<()> {
        self.raw(&[n])
    }
    fn u32(&mut self, n: u32) -> Result<()> {
        self.raw(&n.to_le_bytes())
    }
    fn u64(&mut self, n: u64) -> Result<()> {
        self.raw(&n.to_le_bytes())
    }
    fn bytes(&mut self, bytes: &[u8]) -> Result<()> {
        self.u32(u32::try_from(bytes.len()).map_err(|_| StoreError::LimitExceeded)?)?;
        self.raw(bytes)
    }
}
impl<R: Read> ValueReader for Decoder<R> {
    type Error = StoreError;
    fn byte(&mut self) -> Result<u8> {
        let mut b = [0];
        self.raw(&mut b)?;
        Ok(b[0])
    }
    fn u32(&mut self) -> Result<u32> {
        let mut b = [0; 4];
        self.raw(&mut b)?;
        Ok(u32::from_le_bytes(b))
    }
    fn u64(&mut self) -> Result<u64> {
        let mut b = [0; 8];
        self.raw(&mut b)?;
        Ok(u64::from_le_bytes(b))
    }
    fn bytes(&mut self) -> Result<Vec<u8>> {
        let len = self.u32()? as usize;
        if len as u64 > self.remaining {
            return Err(StoreError::Corrupt);
        }
        self.allocate(len)?;
        let mut bytes = vec![0; len];
        self.raw(&mut bytes)?;
        Ok(bytes)
    }
    fn corrupt(&self) -> StoreError {
        StoreError::Corrupt
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn dense_null_payload_cannot_amplify_into_unbounded_decoded_allocations() {
        let mut encoded = vec![];
        encoded.extend_from_slice(&0_u64.to_le_bytes());
        encoded.push(0);
        encoded.extend_from_slice(&10_000_u32.to_le_bytes());
        encoded.extend_from_slice(&1_u32.to_le_bytes());
        encoded.resize(encoded.len() + 10_000, 0);
        let mut reader = Decoder::new(std::io::Cursor::new(&encoded), encoded.len() as u64, 1024);
        assert!(matches!(
            reader.page(0, 1, 10_000),
            Err(StoreError::LimitExceeded)
        ));
    }
    #[test]
    fn forged_string_length_is_rejected_before_allocation() {
        let mut encoded = vec![];
        encoded.extend_from_slice(&0_u64.to_le_bytes());
        encoded.push(0);
        encoded.extend_from_slice(&1_u32.to_le_bytes());
        encoded.extend_from_slice(&1_u32.to_le_bytes());
        encoded.push(5);
        encoded.extend_from_slice(&u32::MAX.to_le_bytes());
        let mut reader = Decoder::new(std::io::Cursor::new(&encoded), encoded.len() as u64, 1024);
        assert!(matches!(reader.page(0, 1, 1), Err(StoreError::Corrupt)));
    }
}
