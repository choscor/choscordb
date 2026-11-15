use crate::{convert, ffi};
use choscordb_core::{ClipboardFormat, SqlDialect, render_clipboard};
use choscordb_result_store::{
    CopyCell, CopyError, CopyRequest, render_copy_tsv, resolve_copy_rows,
};

const FORMATS: [(&str, &str); 7] = [
    ("tsv", "Tab-separated values"),
    ("csv", "CSV"),
    ("csv_header", "CSV with header"),
    ("markdown", "Markdown table"),
    ("html", "HTML table"),
    ("sql_insert", "SQL INSERT"),
    ("json", "JSON"),
];

pub fn copy_format_options() -> Vec<ffi::CopyFormatOptionDto> {
    FORMATS
        .iter()
        .map(|(code, label)| ffi::CopyFormatOptionDto {
            code: (*code).into(),
            label: (*label).into(),
        })
        .collect()
}

pub(crate) fn sql_dialect(driver: &str) -> Option<SqlDialect> {
    match driver {
        "sqlite" => Some(SqlDialect::Sqlite),
        "postgres" | "postgresql" => Some(SqlDialect::Postgres),
        "mysql" | "mariadb" => Some(SqlDialect::Mysql),
        _ => None,
    }
}

enum Rendered {
    Copy(CopyError),
    Format(String),
}

fn render(mut source: ffi::CopyRequestDto) -> Result<String, Rendered> {
    let format = std::mem::take(&mut source.format);
    let columns = std::mem::take(&mut source.columns);
    let sources = std::mem::take(&mut source.source_tables);
    let dialect = sql_dialect(&source.driver);
    let request = request(source).map_err(Rendered::Copy)?;
    if format == "tsv" {
        return render_copy_tsv(request).map_err(Rendered::Copy);
    }
    let table = match sources.split_first() {
        Some((first, rest)) if rest.iter().all(|source| source == first) => first.as_str(),
        _ => "",
    };
    let budget = request.byte_budget / 2;
    let format = ClipboardFormat::parse(&format, table, dialect)
        .map_err(|error| Rendered::Format(error.message))?;
    let rows = resolve_copy_rows(request).map_err(Rendered::Copy)?;
    let text = render_clipboard(&format, &columns, &rows)
        .map_err(|error| Rendered::Format(error.message))?;
    if text.encode_utf16().count() > budget {
        return Err(Rendered::Copy(CopyError::Limit));
    }
    Ok(text)
}

fn request(source: ffi::CopyRequestDto) -> Result<CopyRequest, CopyError> {
    if !source.valid_unicode {
        return Err(CopyError::InvalidInput);
    }
    let rows = source
        .rows
        .into_iter()
        .map(|row| {
            row.cells
                .into_iter()
                .map(|cell| {
                    if !cell.selected {
                        return Ok(None);
                    }
                    Ok(Some(CopyCell {
                        original: convert::value(cell.original)
                            .map_err(|_| CopyError::InvalidInput)?,
                        resolved: cell
                            .has_resolved
                            .then(|| convert::value(cell.resolved))
                            .transpose()
                            .map_err(|_| CopyError::InvalidInput)?,
                        inserted_omitted: cell.inserted_omitted,
                    }))
                })
                .collect::<Result<Vec<_>, _>>()
        })
        .collect::<Result<Vec<_>, _>>()?;
    let resolutions = source
        .resolutions
        .into_iter()
        .map(|resolution| {
            Ok((
                resolution
                    .has_original
                    .then(|| convert::value(resolution.original))
                    .transpose()
                    .map_err(|_| CopyError::InvalidInput)?,
                convert::value(resolution.resolved).map_err(|_| CopyError::InvalidInput)?,
            ))
        })
        .collect::<Result<Vec<_>, _>>()?;
    Ok(CopyRequest {
        rows,
        resolutions,
        byte_budget: usize::try_from(source.byte_budget).map_err(|_| CopyError::Limit)?,
    })
}

pub fn render_copy_policy(source: ffi::CopyRequestDto) -> ffi::CopyResultDto {
    match render(source) {
        Ok(text) => ffi::CopyResultDto {
            text,
            ..Default::default()
        },
        Err(Rendered::Format(reason)) => ffi::CopyResultDto {
            error: "invalid_format".into(),
            reason,
            ..Default::default()
        },
        Err(Rendered::Copy(error)) => {
            let (code, database_type, reason) = match error {
                CopyError::InvalidInput => ("invalid_input", String::new(), String::new()),
                CopyError::InvalidResolution => {
                    ("invalid_resolution", String::new(), String::new())
                }
                CopyError::WrongResolutionType => {
                    ("wrong_resolution_type", String::new(), String::new())
                }
                CopyError::IncompleteResolution => {
                    ("incomplete_resolution", String::new(), String::new())
                }
                CopyError::Unavailable {
                    database_type,
                    reason,
                } => ("unavailable", database_type, reason),
                CopyError::Deferred => ("deferred", String::new(), String::new()),
                CopyError::Limit => ("limit", String::new(), String::new()),
            };
            ffi::CopyResultDto {
                error: code.into(),
                database_type,
                reason,
                ..Default::default()
            }
        }
    }
}
