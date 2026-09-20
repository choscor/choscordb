//! Incremental encoding keeps at most a raw chunk, UTF-8 carry, and escaped chunk.
use crate::{ExportSink, ExportSource, Limits, format::*};
use choscordb_driver_api::*;
fn invalid() -> DriverError {
    DriverError::new(
        ErrorKind::InvalidInput,
        "Invalid deferred export value or chunk",
    )
}
struct Counter<'a> {
    bytes: u64,
    reported: u64,
    base: crate::Progress,
    sender: Option<&'a tokio::sync::mpsc::Sender<crate::Progress>>,
}
async fn write(sink: &mut dyn ExportSink, bytes: &str, count: &mut Counter<'_>) -> Result<()> {
    sink.write(bytes.as_bytes()).await?;
    count.bytes = count
        .bytes
        .checked_add(bytes.len() as u64)
        .ok_or_else(invalid)?;
    if count.bytes.saturating_sub(count.reported) >= 64 * 1024 {
        count.reported = count.bytes;
        if let Some(sender) = count.sender {
            let _ = sender.try_send(crate::Progress {
                rows: count.base.rows,
                bytes: count
                    .base
                    .bytes
                    .checked_add(count.bytes)
                    .ok_or_else(invalid)?,
            });
        }
    }
    Ok(())
}
#[derive(Clone, Copy)]
pub(crate) struct Config<'a> {
    pub limits: Limits,
    pub progress: crate::Progress,
    pub sender: Option<&'a tokio::sync::mpsc::Sender<crate::Progress>>,
    pub cancel: &'a crate::Cancellation,
}
pub(crate) async fn row(
    source: &mut dyn ExportSource,
    sink: &mut dyn ExportSink,
    format: &ExportFormat,
    columns: &[Column],
    values: &Row,
    first: bool,
    config: Config<'_>,
) -> Result<u64> {
    let limits = config.limits;
    if values.len() != columns.len() {
        return Err(invalid());
    }
    let mut count = Counter {
        bytes: 0,
        reported: 0,
        base: config.progress,
        sender: config.sender,
    };
    let prefix = match format {
        ExportFormat::Csv => String::new(),
        ExportFormat::Json => if first { "[" } else { ",[" }.into(),
        ExportFormat::JsonLines => "{\"row\":[".into(),
        ExportFormat::SqlInsert { table, dialect } => format!(
            "INSERT INTO {} ({}) VALUES (",
            table
                .iter()
                .map(|v| identifier(v, *dialect))
                .collect::<Result<Vec<_>>>()?
                .join("."),
            columns
                .iter()
                .map(|v| identifier(&v.name, *dialect))
                .collect::<Result<Vec<_>>>()?
                .join(", ")
        ),
    };
    write(sink, &prefix, &mut count).await?;
    for (index, value) in values.iter().enumerate() {
        if index != 0 {
            write(
                sink,
                if matches!(format, ExportFormat::SqlInsert { .. }) {
                    ", "
                } else {
                    ","
                },
                &mut count,
            )
            .await?;
        }
        if let Value::Deferred {
            handle,
            byte_length,
            ..
        } = value
        {
            deferred(
                source,
                sink,
                format,
                *handle,
                *byte_length,
                config,
                &mut count,
            )
            .await?;
        } else {
            if value.estimated_bytes() > limits.value_bytes {
                return Err(DriverError::new(
                    ErrorKind::ResourceLimit,
                    "Value exceeds export limit",
                ));
            }
            crate::budget::row(format, columns, std::slice::from_ref(value), limits)?;
            let encoded = match format {
                ExportFormat::Csv if matches!(value, Value::Null) => String::new(),
                ExportFormat::Csv => csv_field(&plain(value)?),
                ExportFormat::Json | ExportFormat::JsonLines => json(value)?,
                ExportFormat::SqlInsert { dialect, .. } => sql(value, *dialect)?,
            };
            write(sink, &encoded, &mut count).await?;
        }
    }
    write(
        sink,
        match format {
            ExportFormat::Csv => "\r\n",
            ExportFormat::Json => "]",
            ExportFormat::JsonLines => "]}\n",
            ExportFormat::SqlInsert { .. } => ");\n",
        },
        &mut count,
    )
    .await?;
    Ok(count.bytes)
}
async fn deferred(
    source: &mut dyn ExportSource,
    sink: &mut dyn ExportSink,
    format: &ExportFormat,
    handle: Handle,
    total: u64,
    config: Config<'_>,
    count: &mut Counter<'_>,
) -> Result<()> {
    let Config { limits, cancel, .. } = config;
    let max = MAX_VALUE_CHUNK_BYTES
        .min(limits.value_bytes)
        .min(limits.page_bytes)
        .min(limits.encoded_row_bytes.saturating_sub(256) / 32);
    if max == 0 {
        return Err(DriverError::new(
            ErrorKind::ResourceLimit,
            "No deferred chunk capacity",
        ));
    }
    let mut offset = 0;
    let mut kind = None;
    let mut carry = Vec::with_capacity(4);
    loop {
        if cancel.is_cancelled() {
            return Err(DriverError::new(ErrorKind::Cancelled, "Export cancelled"));
        }
        let chunk = source.read_value_chunk(handle, offset, max).await?;
        if chunk.offset != offset
            || chunk.total_bytes != total
            || chunk.bytes.len() > max
            || chunk.bytes.len() as u64 > total.saturating_sub(offset)
            || (chunk.bytes.is_empty() && offset != total)
            || kind.is_some_and(|k| k != chunk.kind)
        {
            return Err(invalid());
        }
        if kind.is_none() {
            kind = Some(chunk.kind);
            let prefix = match (format, chunk.kind) {
                (ExportFormat::Csv, DeferredKind::Binary) => "\"\\x",
                (
                    ExportFormat::Csv | ExportFormat::Json | ExportFormat::JsonLines,
                    DeferredKind::Text,
                ) => "\"",
                (ExportFormat::Json | ExportFormat::JsonLines, DeferredKind::Binary) => {
                    "{\"binary_hex\":\""
                }
                (
                    ExportFormat::SqlInsert {
                        dialect: SqlDialect::Sqlite | SqlDialect::Mysql,
                        ..
                    },
                    DeferredKind::Binary,
                ) => "X'",
                (
                    ExportFormat::SqlInsert {
                        dialect: SqlDialect::Postgres,
                        ..
                    },
                    DeferredKind::Binary,
                ) => "decode('",
                (
                    ExportFormat::SqlInsert {
                        dialect: SqlDialect::Sqlite,
                        ..
                    },
                    DeferredKind::Text,
                ) => "'",
                (
                    ExportFormat::SqlInsert {
                        dialect: SqlDialect::Postgres,
                        ..
                    },
                    DeferredKind::Text,
                ) => "E'",
                (
                    ExportFormat::SqlInsert {
                        dialect: SqlDialect::Mysql,
                        ..
                    },
                    DeferredKind::Text,
                ) => "CONVERT(X'",
            };
            write(sink, prefix, count).await?;
        }
        offset += chunk.bytes.len() as u64;
        if chunk.kind == DeferredKind::Binary {
            write(sink, &hex(&chunk.bytes), count).await?;
        } else {
            let mut bytes = Vec::with_capacity(carry.len() + chunk.bytes.len());
            bytes.extend_from_slice(&carry);
            bytes.extend_from_slice(&chunk.bytes);
            carry.clear();
            let valid = match std::str::from_utf8(&bytes) {
                Ok(s) => s,
                Err(e) if e.error_len().is_none() && offset < total => {
                    carry.extend_from_slice(&bytes[e.valid_up_to()..]);
                    std::str::from_utf8(&bytes[..e.valid_up_to()]).map_err(|_| invalid())?
                }
                Err(_) => return Err(invalid()),
            };
            let encoded = match format {
                ExportFormat::Csv => valid.replace('"', "\"\""),
                ExportFormat::Json | ExportFormat::JsonLines => {
                    let s = serde_json::to_string(valid).map_err(|_| invalid())?;
                    s[1..s.len() - 1].to_owned()
                }
                ExportFormat::SqlInsert { dialect, .. } => {
                    if valid.contains('\0') && !matches!(dialect, SqlDialect::Mysql) {
                        return Err(invalid());
                    }
                    match dialect {
                        SqlDialect::Sqlite => valid.replace('\'', "''"),
                        SqlDialect::Mysql => hex(valid.as_bytes()),
                        SqlDialect::Postgres => valid.replace('\\', "\\\\").replace('\'', "''"),
                    }
                }
            };
            write(sink, &encoded, count).await?;
        }
        if offset == total {
            break;
        }
    }
    let suffix = match (format, kind.ok_or_else(invalid)?) {
        (ExportFormat::Csv, _)
        | (ExportFormat::Json | ExportFormat::JsonLines, DeferredKind::Text) => "\"",
        (ExportFormat::Json | ExportFormat::JsonLines, DeferredKind::Binary) => "\"}",
        (
            ExportFormat::SqlInsert {
                dialect: SqlDialect::Postgres,
                ..
            },
            DeferredKind::Binary,
        ) => "', 'hex')",
        (
            ExportFormat::SqlInsert {
                dialect: SqlDialect::Mysql,
                ..
            },
            DeferredKind::Text,
        ) => "' USING utf8mb4)",
        (ExportFormat::SqlInsert { .. }, _) => "'",
    };
    write(sink, suffix, count).await
}
