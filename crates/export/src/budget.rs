//! Allocation-free conservative preflight. No escaping or schema cloning occurs here.
use crate::{ExportFormat, Limits};
use choscordb_driver_api::*;
fn limit() -> DriverError {
    DriverError::new(
        ErrorKind::ResourceLimit,
        "Export encoding exceeds conservative memory budget",
    )
}
fn add(a: usize, b: usize) -> Result<usize> {
    a.checked_add(b).ok_or_else(limit)
}
fn mul(a: usize, b: usize) -> Result<usize> {
    a.checked_mul(b).ok_or_else(limit)
}
fn strings(c: &Column) -> Result<usize> {
    add(
        add(c.name.len(), c.database_type.len())?,
        c.timezone.as_ref().map_or(0, String::len),
    )
}
fn encoded_schema(columns: &[Column]) -> Result<usize> {
    columns
        .iter()
        .try_fold(128, |n, c| add(n, add(256, mul(strings(c)?, 6)?)?))
}
fn table_bytes(format: &ExportFormat) -> Result<usize> {
    match format {
        ExportFormat::SqlInsert { table, .. } => table
            .iter()
            .try_fold(0, |n, s| add(n, add(4, mul(s.len(), 2)?)?)),
        _ => Ok(0),
    }
}
/// Check source schema before copying it. Four times the encoded bound reserves
/// room for replacement strings, per-field vectors, joining, and final formatting.
pub fn schema(format: &ExportFormat, columns: &[Column], limits: Limits) -> Result<()> {
    let copied = columns.iter().try_fold(
        mul(columns.len(), std::mem::size_of::<Column>())?,
        |n, c| add(n, strings(c)?),
    )?;
    let encoding = add(encoded_schema(columns)?, table_bytes(format)?)?;
    if copied > limits.page_bytes || mul(encoding, 4)? > limits.encoded_row_bytes {
        Err(limit())
    } else {
        Ok(())
    }
}
/// Validate an encoding budget without allocating an encoded string. The bound
/// intentionally overestimates escaping and temporary copies for every format.
pub fn row(
    format: &ExportFormat,
    columns: &[Column],
    values: &[Value],
    limits: Limits,
) -> Result<()> {
    let mut bound = 128usize;
    if matches!(format, ExportFormat::SqlInsert { .. }) {
        bound = add(bound, add(encoded_schema(columns)?, table_bytes(format)?)?)?;
    }
    for value in values {
        let bytes = match value {
            Value::Text(s)
            | Value::Decimal(s)
            | Value::Date(s)
            | Value::Time(s)
            | Value::Timestamp(s)
            | Value::Uuid(s)
            | Value::Json(s) => s.len(),
            Value::Binary(b) => b.len(),
            Value::Deferred { .. } => return Err(limit()),
            _ => 32,
        };
        bound = add(bound, add(128, mul(bytes, 6)?)?)?;
    }
    if mul(bound, 4)? > limits.encoded_row_bytes {
        Err(limit())
    } else {
        Ok(())
    }
}
