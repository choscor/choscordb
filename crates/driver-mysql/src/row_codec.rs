//! The binary form of a spooled result row.
use choscordb_driver_api::*;

fn corrupt() -> DriverError {
    DriverError::new(ErrorKind::Internal, "Invalid MySQL result storage")
}

struct Writer(Vec<u8>);
impl ValueWriter for Writer {
    type Error = DriverError;
    fn byte(&mut self, n: u8) -> Result<()> {
        self.0.push(n);
        Ok(())
    }
    fn u32(&mut self, n: u32) -> Result<()> {
        self.0.extend_from_slice(&n.to_le_bytes());
        Ok(())
    }
    fn u64(&mut self, n: u64) -> Result<()> {
        self.0.extend_from_slice(&n.to_le_bytes());
        Ok(())
    }
    fn bytes(&mut self, bytes: &[u8]) -> Result<()> {
        let length = u32::try_from(bytes.len()).map_err(|_| {
            DriverError::new(ErrorKind::ResourceLimit, "MySQL result value is too large")
        })?;
        self.u32(length)?;
        self.0.extend_from_slice(bytes);
        Ok(())
    }
}

struct Reader<'a>(&'a [u8]);
impl Reader<'_> {
    fn take(&mut self, count: usize) -> Result<&[u8]> {
        if count > self.0.len() {
            return Err(corrupt());
        }
        let (head, tail) = self.0.split_at(count);
        self.0 = tail;
        Ok(head)
    }
}
impl ValueReader for Reader<'_> {
    type Error = DriverError;
    fn byte(&mut self) -> Result<u8> {
        Ok(self.take(1)?[0])
    }
    fn u32(&mut self) -> Result<u32> {
        Ok(u32::from_le_bytes(
            self.take(4)?.try_into().map_err(|_| corrupt())?,
        ))
    }
    fn u64(&mut self) -> Result<u64> {
        Ok(u64::from_le_bytes(
            self.take(8)?.try_into().map_err(|_| corrupt())?,
        ))
    }
    fn bytes(&mut self) -> Result<Vec<u8>> {
        let length = self.u32()? as usize;
        Ok(self.take(length)?.to_vec())
    }
    fn corrupt(&self) -> DriverError {
        corrupt()
    }
}

pub(crate) fn encode(row: &[Value]) -> Result<Vec<u8>> {
    let mut out = Writer(Vec::new());
    for value in row {
        write_value(&mut out, value)?;
    }
    Ok(out.0)
}

/// Reads a row of `columns` values. The bytes must hold exactly that row.
pub(crate) fn decode(bytes: &[u8], columns: usize) -> Result<Row> {
    let mut input = Reader(bytes);
    let mut row = Vec::with_capacity(columns);
    for _ in 0..columns {
        row.push(read_value(&mut input)?);
    }
    if !input.0.is_empty() {
        return Err(corrupt());
    }
    Ok(row)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn every_kind() -> Row {
        let handle = Handle {
            slot: 7,
            generation: 3,
        };
        vec![
            Value::Null,
            Value::Bool(true),
            Value::Integer(-42),
            Value::Real(1.5),
            Value::Decimal("12.30".into()),
            Value::Text("é \"quoted\"\n".into()),
            Value::Date("2026-10-27".into()),
            Value::Time("12:00:01".into()),
            Value::Timestamp("2026-10-27 12:00:01".into()),
            Value::Uuid("6f9619ff-8b86-d011-b42d-00cf4fc964ff".into()),
            Value::Json("{\"a\":1}".into()),
            Value::Binary(vec![0, 255, 1]),
            Value::Deferred {
                handle,
                byte_length: 90_000,
                database_type: "TEXT".into(),
            },
            Value::FallbackText {
                text: "POINT(1 2)".into(),
                database_type: "GEOMETRY".into(),
            },
            Value::DeferredFallback {
                handle,
                byte_length: 80_000,
                database_type: "GEOMETRY".into(),
            },
            Value::Unavailable {
                database_type: "BIT".into(),
                reason: "unsupported".into(),
            },
        ]
    }

    #[test]
    fn a_row_of_each_value_kind_reads_back_unchanged() {
        let row = every_kind();
        let bytes = encode(&row).unwrap();
        assert_eq!(decode(&bytes, row.len()).unwrap(), row);
    }

    #[test]
    fn a_truncated_or_padded_row_is_an_error() {
        let row = every_kind();
        let bytes = encode(&row).unwrap();
        assert!(decode(&bytes[..bytes.len() - 1], row.len()).is_err());
        let mut padded = bytes.clone();
        padded.push(0);
        assert!(decode(&padded, row.len()).is_err());
        // A forged length larger than the record is rejected before it allocates.
        let mut forged = vec![5];
        forged.extend_from_slice(&u32::MAX.to_le_bytes());
        assert!(decode(&forged, 1).is_err());
    }
}
