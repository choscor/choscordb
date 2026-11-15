use crate::format::{encode_row, json, plain};
use crate::{ExportFormat, SqlDialect};
use choscordb_driver_api::{Column, DriverError, ErrorKind, Result, Value};

pub const CLIPBOARD_SQL_TABLE_REQUIRED: &str =
    "Copy as SQL INSERT needs columns from one table. Select columns of one table.";

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ClipboardFormat {
    Csv { header: bool },
    Markdown,
    Html,
    SqlInsert { table: String, dialect: SqlDialect },
    Json,
}

fn invalid(message: &str) -> DriverError {
    DriverError::new(ErrorKind::InvalidInput, message)
}

impl ClipboardFormat {
    pub fn parse(code: &str, table: &str, dialect: Option<SqlDialect>) -> Result<Self> {
        Ok(match code {
            "csv" => Self::Csv { header: false },
            "csv_header" => Self::Csv { header: true },
            "markdown" => Self::Markdown,
            "html" => Self::Html,
            "json" => Self::Json,
            "sql_insert" => Self::SqlInsert {
                table: table.into(),
                dialect: dialect.ok_or_else(|| invalid("Unknown SQL dialect"))?,
            },
            _ => return Err(invalid("Unknown copy format")),
        })
    }
}

fn quoted_path(text: &str, dialect: SqlDialect) -> Option<Vec<String>> {
    let quote = if matches!(dialect, SqlDialect::Mysql) {
        '`'
    } else {
        '"'
    };
    let mut parts = Vec::new();
    let mut chars = text.chars().peekable();
    loop {
        if chars.next()? != quote {
            return None;
        }
        let mut part = String::new();
        loop {
            let character = chars.next()?;
            if character != quote {
                part.push(character);
            } else if chars.peek() == Some(&quote) {
                chars.next();
                part.push(quote);
            } else {
                break;
            }
        }
        parts.push(part);
        match chars.next() {
            None => break,
            Some('.') => continue,
            Some(_) => return None,
        }
    }
    (1..=3).contains(&parts.len()).then_some(parts)
}

fn export_columns(names: &[String]) -> Vec<Column> {
    names
        .iter()
        .map(|name| Column {
            name: name.clone(),
            database_type: String::new(),
            precision: None,
            scale: None,
            timezone: None,
            nullable: None,
        })
        .collect()
}

fn text(value: &Value) -> Result<String> {
    if matches!(value, Value::Null) {
        return Ok("NULL".into());
    }
    plain(value)
}

fn markdown_cell(value: &str) -> String {
    value
        .replace('\\', "\\\\")
        .replace('|', "\\|")
        .replace("\r\n", "<br>")
        .replace(['\n', '\r'], "<br>")
}

fn html_cell(value: &str) -> String {
    value
        .replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}

pub fn render_clipboard(
    format: &ClipboardFormat,
    columns: &[String],
    rows: &[Vec<Value>],
) -> Result<String> {
    if columns.is_empty() || rows.iter().any(|row| row.len() != columns.len()) {
        return Err(invalid("Copied rows do not match their columns"));
    }
    let mut output = String::new();
    match format {
        ClipboardFormat::Csv { header } => {
            let format = ExportFormat::Csv(Default::default());
            let columns = export_columns(columns);
            if *header {
                output.push_str(&crate::format::header(&format, &columns)?);
            }
            for row in rows {
                output.push_str(&encode_row(&format, &columns, row, false)?);
            }
        }
        ClipboardFormat::Markdown => {
            let header = columns.iter().map(|name| markdown_cell(name));
            output.push_str(&format!("| {} |\n", header.collect::<Vec<_>>().join(" | ")));
            output.push_str(&format!("|{}\n", " --- |".repeat(columns.len())));
            for row in rows {
                let cells = row
                    .iter()
                    .map(|value| text(value).map(|text| markdown_cell(&text)))
                    .collect::<Result<Vec<_>>>()?;
                output.push_str(&format!("| {} |\n", cells.join(" | ")));
            }
        }
        ClipboardFormat::Html => {
            output.push_str("<table>\n<thead><tr>");
            for name in columns {
                output.push_str(&format!("<th>{}</th>", html_cell(name)));
            }
            output.push_str("</tr></thead>\n<tbody>\n");
            for row in rows {
                output.push_str("<tr>");
                for value in row {
                    output.push_str(&format!("<td>{}</td>", html_cell(&text(value)?)));
                }
                output.push_str("</tr>\n");
            }
            output.push_str("</tbody>\n</table>\n");
        }
        ClipboardFormat::SqlInsert { table, dialect } => {
            let format = ExportFormat::SqlInsert {
                table: quoted_path(table, *dialect)
                    .ok_or_else(|| invalid(CLIPBOARD_SQL_TABLE_REQUIRED))?,
                dialect: *dialect,
            };
            let columns = export_columns(columns);
            crate::format::header(&format, &columns)?;
            for row in rows {
                output.push_str(&encode_row(&format, &columns, row, false)?);
            }
        }
        ClipboardFormat::Json => {
            let keys = columns
                .iter()
                .map(|name| serde_json::to_string(name).map_err(|_| invalid("Invalid column")))
                .collect::<Result<Vec<_>>>()?;
            output.push_str("[\n");
            for (index, row) in rows.iter().enumerate() {
                let fields = keys
                    .iter()
                    .zip(row)
                    .map(|(key, value)| json(value).map(|value| format!("{key}:{value}")))
                    .collect::<Result<Vec<_>>>()?;
                let separator = if index + 1 < rows.len() { "," } else { "" };
                output.push_str(&format!("  {{{}}}{separator}\n", fields.join(",")));
            }
            output.push_str("]\n");
        }
    }
    Ok(output)
}
