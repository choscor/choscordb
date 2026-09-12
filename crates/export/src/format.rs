use choscordb_driver_api::*;
#[derive(Clone, Copy, Debug)]
pub enum SqlDialect {
    Sqlite,
    Postgres,
}
#[derive(Clone, Debug)]
pub enum ExportFormat {
    Csv,
    Json,
    JsonLines,
    SqlInsert {
        table: Vec<String>,
        dialect: SqlDialect,
    },
}
/// Always quote non-NULL fields. An unquoted empty field is NULL; "" is empty text.
pub fn csv_field(value: &str) -> String {
    format!("\"{}\"", value.replace('"', "\"\""))
}
fn invalid(message: &str) -> DriverError {
    DriverError::new(ErrorKind::InvalidInput, message)
}
pub(crate) fn hex(bytes: &[u8]) -> String {
    const HEX: &[u8] = b"0123456789abcdef";
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        s.push(HEX[(b >> 4) as usize] as char);
        s.push(HEX[(b & 15) as usize] as char);
    }
    s
}
fn quoted(value: &str) -> String {
    serde_json::to_string(value).expect("string serialization is infallible")
}
/// Strict JSON numeric grammar; excludes SQL tokens and preserves digits verbatim.
pub fn valid_decimal(value: &str) -> bool {
    let b = value.as_bytes();
    let mut i = usize::from(b.first() == Some(&b'-'));
    match b.get(i) {
        Some(b'0') => i += 1,
        Some(b'1'..=b'9') => {
            i += 1;
            while b.get(i).is_some_and(u8::is_ascii_digit) {
                i += 1;
            }
        }
        _ => return false,
    }
    if b.get(i) == Some(&b'.') {
        i += 1;
        let start = i;
        while b.get(i).is_some_and(u8::is_ascii_digit) {
            i += 1;
        }
        if start == i {
            return false;
        }
    }
    if matches!(b.get(i), Some(b'e' | b'E')) {
        i += 1;
        if matches!(b.get(i), Some(b'+' | b'-')) {
            i += 1;
        }
        let start = i;
        while b.get(i).is_some_and(u8::is_ascii_digit) {
            i += 1;
        }
        if start == i {
            return false;
        }
    }
    i == b.len()
}
pub(crate) fn plain(value: &Value) -> Result<String> {
    Ok(match value {
        Value::Null => String::new(),
        Value::Bool(v) => v.to_string(),
        Value::Integer(v) => v.to_string(),
        Value::Real(v) => v.to_string(),
        Value::Decimal(v)
        | Value::Text(v)
        | Value::Date(v)
        | Value::Time(v)
        | Value::Timestamp(v)
        | Value::Uuid(v)
        | Value::Json(v) => v.clone(),
        Value::Binary(v) => format!("\\x{}", hex(v)),
        Value::Deferred { .. } => return Err(invalid("Deferred value was not resolved")),
    })
}
pub(crate) fn json(value: &Value) -> Result<String> {
    Ok(match value {
        Value::Null => "null".into(),
        Value::Bool(v) => v.to_string(),
        Value::Integer(v) => v.to_string(),
        Value::Real(v) if v.is_finite() => {
            serde_json::to_string(v).map_err(|_| invalid("Invalid floating value"))?
        }
        Value::Real(v) => format!("{{\"float\":{}}}", quoted(&v.to_string())),
        Value::Decimal(v) => format!("{{\"decimal\":{}}}", quoted(v)),
        Value::Binary(v) => format!("{{\"binary_hex\":{}}}", quoted(&hex(v))),
        Value::Json(v) => format!("{{\"json\":{}}}", quoted(v)),
        Value::Deferred { .. } => return Err(invalid("Deferred value was not resolved")),
        _ => quoted(&plain(value)?),
    })
}
pub(crate) fn identifier(value: &str) -> Result<String> {
    if value.is_empty() || value.contains('\0') {
        return Err(invalid("Invalid SQL identifier"));
    }
    Ok(format!("\"{}\"", value.replace('"', "\"\"")))
}
pub(crate) fn sql(value: &Value, dialect: SqlDialect) -> Result<String> {
    Ok(match value {
        Value::Null => "NULL".into(),
        Value::Bool(v) => if *v { "TRUE" } else { "FALSE" }.into(),
        Value::Integer(v) => v.to_string(),
        Value::Real(v) if v.is_finite() => v.to_string(),
        Value::Real(_) => {
            return Err(invalid(
                "Nonfinite floating values require a database-specific cast",
            ));
        }
        Value::Decimal(v) if valid_decimal(v) => v.clone(),
        Value::Decimal(_) => return Err(invalid("Invalid exact decimal representation")),
        Value::Binary(v) => match dialect {
            SqlDialect::Sqlite => format!("X'{}'", hex(v)),
            SqlDialect::Postgres => format!("decode('{}', 'hex')", hex(v)),
        },
        Value::Deferred { .. } => return Err(invalid("Deferred value was not resolved")),
        _ => {
            let value = plain(value)?;
            if value.contains('\0') {
                return Err(invalid("SQL text literal contains NUL"));
            }
            match dialect {
                SqlDialect::Sqlite => format!("'{}'", value.replace('\'', "''")),
                SqlDialect::Postgres => {
                    format!("E'{}'", value.replace('\\', "\\\\").replace('\'', "''"))
                }
            }
        }
    })
}
pub(crate) fn header(format: &ExportFormat, columns: &[Column]) -> Result<String> {
    match format {
        ExportFormat::Csv => Ok(format!(
            "{}\r\n",
            columns
                .iter()
                .map(|c| csv_field(&c.name))
                .collect::<Vec<_>>()
                .join(",")
        )),
        ExportFormat::Json => Ok(format!(
            "{{\"columns\":{},\"rows\":[",
            serde_json::to_string(columns).map_err(|_| invalid("Invalid columns"))?
        )),
        ExportFormat::JsonLines => Ok(format!(
            "{{\"columns\":{}}}\n",
            serde_json::to_string(columns).map_err(|_| invalid("Invalid columns"))?
        )),
        ExportFormat::SqlInsert { table, .. } => {
            if table.is_empty() {
                return Err(invalid("SQL export requires a table name"));
            }
            for part in table {
                identifier(part)?;
            }
            for column in columns {
                identifier(&column.name)?;
            }
            Ok(String::new())
        }
    }
}
pub fn encode_row(
    format: &ExportFormat,
    columns: &[Column],
    row: &Row,
    first: bool,
) -> Result<String> {
    if row.len() != columns.len() {
        return Err(invalid("Export row does not match its schema"));
    }
    match format {
        ExportFormat::Csv => Ok(format!(
            "{}\r\n",
            row.iter()
                .map(|v| if matches!(v, Value::Null) {
                    Ok(String::new())
                } else {
                    plain(v).map(|v| csv_field(&v))
                })
                .collect::<Result<Vec<_>>>()?
                .join(",")
        )),
        ExportFormat::Json | ExportFormat::JsonLines => {
            let values = row.iter().map(json).collect::<Result<Vec<_>>>()?.join(",");
            Ok(match format {
                ExportFormat::Json => format!("{}[{}]", if first { "" } else { "," }, values),
                _ => format!("{{\"row\":[{values}]}}\n"),
            })
        }
        ExportFormat::SqlInsert { table, dialect } => Ok(format!(
            "INSERT INTO {} ({}) VALUES ({});\n",
            table
                .iter()
                .map(|s| identifier(s))
                .collect::<Result<Vec<_>>>()?
                .join("."),
            columns
                .iter()
                .map(|c| identifier(&c.name))
                .collect::<Result<Vec<_>>>()?
                .join(", "),
            row.iter()
                .map(|v| sql(v, *dialect))
                .collect::<Result<Vec<_>>>()?
                .join(", ")
        )),
    }
}
pub(crate) fn footer(format: &ExportFormat) -> &'static str {
    if matches!(format, ExportFormat::Json) {
        "]}"
    } else {
        ""
    }
}
