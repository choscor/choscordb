//! Typed transport for result JSON rendering owned by result-store.
use crate::{convert, ffi};
use choscordb_driver_api::{Column, Value};
use choscordb_result_store::{
    JsonViewError, JsonViewReadiness, JsonViewRow, json_cell_readiness, json_page_readiness,
    json_row_readiness, render_json_cell, render_json_page, render_json_row,
};
use std::collections::BTreeMap;

fn column(source: ffi::ColumnDto) -> Column {
    Column {
        name: source.name,
        database_type: source.database_type,
        precision: source.has_precision.then_some(source.precision),
        scale: source.has_scale.then_some(source.scale),
        timezone: (!source.timezone.is_empty()).then_some(source.timezone),
        nullable: match source.nullability {
            0 => Some(false),
            1 => Some(true),
            _ => None,
        },
    }
}

fn value(source: ffi::CellDto) -> Result<Value, JsonViewError> {
    convert::value(source).map_err(|_| JsonViewError::InvalidInput)
}

fn row(source: ffi::JsonViewRowDto) -> Result<JsonViewRow, JsonViewError> {
    Ok(JsonViewRow {
        cells: source
            .cells
            .into_iter()
            .map(value)
            .collect::<Result<_, _>>()?,
        inserted: source.inserted,
        touched: source.touched.into_iter().map(|flag| flag != 0).collect(),
    })
}

fn readiness(value: JsonViewReadiness) -> String {
    match value {
        JsonViewReadiness::Ready => "ready",
        JsonViewReadiness::NeedsDeferred => "needs_deferred",
        JsonViewReadiness::Invalid => "invalid",
        JsonViewReadiness::Unavailable => "unavailable",
    }
    .into()
}

fn error_code(error: JsonViewError) -> String {
    match error {
        JsonViewError::InvalidInput => "invalid_input",
        JsonViewError::InvalidResolution => "invalid_resolution",
        JsonViewError::IncompleteResolution => "incomplete_resolution",
        JsonViewError::InvalidJson => "invalid_json",
        JsonViewError::Unavailable => "unavailable",
        JsonViewError::Deferred => "deferred",
        JsonViewError::NonFinite => "non_finite",
        JsonViewError::DisplayLimit => "display_limit",
        JsonViewError::EncodedLimit => "encoded_limit",
    }
    .into()
}

fn output(result: Result<String, JsonViewError>) -> ffi::JsonViewResultDto {
    match result {
        Ok(json) => ffi::JsonViewResultDto {
            json,
            error: String::new(),
        },
        Err(error) => ffi::JsonViewResultDto {
            error: error_code(error),
            ..Default::default()
        },
    }
}

fn budget(bytes: u64) -> usize {
    usize::try_from(bytes).unwrap_or(usize::MAX)
}

pub fn json_cell_readiness_policy(
    column_dto: ffi::ColumnDto,
    cell_dto: ffi::CellDto,
    valid_unicode: bool,
) -> String {
    if !valid_unicode {
        return readiness(JsonViewReadiness::Invalid);
    }
    match value(cell_dto) {
        Ok(cell) => readiness(json_cell_readiness(&column(column_dto), &cell)),
        Err(_) => readiness(JsonViewReadiness::Invalid),
    }
}

pub fn json_row_readiness_policy(
    columns: Vec<ffi::ColumnDto>,
    source: ffi::JsonViewRowDto,
    budget_bytes: u64,
    valid_unicode: bool,
) -> String {
    if !valid_unicode {
        return readiness(JsonViewReadiness::Invalid);
    }
    match row(source) {
        Ok(row) => readiness(json_row_readiness(
            &columns.into_iter().map(column).collect::<Vec<_>>(),
            &row,
            budget(budget_bytes),
        )),
        Err(_) => readiness(JsonViewReadiness::Invalid),
    }
}

pub fn json_page_readiness_policy(
    columns: Vec<ffi::ColumnDto>,
    source: Vec<ffi::JsonViewRowDto>,
    budget_bytes: u64,
    valid_unicode: bool,
) -> String {
    if !valid_unicode {
        return readiness(JsonViewReadiness::Invalid);
    }
    let rows = source.into_iter().map(row).collect::<Result<Vec<_>, _>>();
    match rows {
        Ok(rows) => readiness(json_page_readiness(
            &columns.into_iter().map(column).collect::<Vec<_>>(),
            &rows,
            budget(budget_bytes),
        )),
        Err(_) => readiness(JsonViewReadiness::Invalid),
    }
}

pub fn render_json_cell_policy(
    column_dto: ffi::ColumnDto,
    cell_dto: ffi::CellDto,
    resolved: Vec<ffi::CellDto>,
    budget_bytes: u64,
    valid_unicode: bool,
) -> ffi::JsonViewResultDto {
    if !valid_unicode || resolved.len() > 1 {
        return output(Err(JsonViewError::InvalidInput));
    }
    output((|| {
        let original = value(cell_dto)?;
        let resolved = resolved.into_iter().next().map(value).transpose()?;
        render_json_cell(
            &column(column_dto),
            &original,
            resolved.as_ref(),
            budget(budget_bytes),
        )
    })())
}

pub fn render_json_row_policy(
    columns: Vec<ffi::ColumnDto>,
    row_dto: ffi::JsonViewRowDto,
    resolved: Vec<ffi::JsonResolvedCellDto>,
    budget_bytes: u64,
    valid_unicode: bool,
) -> ffi::JsonViewResultDto {
    if !valid_unicode {
        return output(Err(JsonViewError::InvalidInput));
    }
    output((|| {
        let columns = columns.into_iter().map(column).collect::<Vec<_>>();
        let row = row(row_dto)?;
        let mut values = BTreeMap::new();
        for entry in resolved {
            if entry.row != 0
                || values
                    .insert(entry.column as usize, value(entry.value)?)
                    .is_some()
            {
                return Err(JsonViewError::InvalidResolution);
            }
        }
        render_json_row(&columns, &row, &values, budget(budget_bytes))
    })())
}

pub fn render_json_page_policy(
    columns: Vec<ffi::ColumnDto>,
    rows: Vec<ffi::JsonViewRowDto>,
    resolved: Vec<ffi::JsonResolvedCellDto>,
    budget_bytes: u64,
    valid_unicode: bool,
) -> ffi::JsonViewResultDto {
    if !valid_unicode {
        return output(Err(JsonViewError::InvalidInput));
    }
    output((|| {
        let columns = columns.into_iter().map(column).collect::<Vec<_>>();
        let rows = rows.into_iter().map(row).collect::<Result<Vec<_>, _>>()?;
        let mut values = BTreeMap::new();
        for entry in resolved {
            if values
                .insert(
                    (entry.row as usize, entry.column as usize),
                    value(entry.value)?,
                )
                .is_some()
            {
                return Err(JsonViewError::InvalidResolution);
            }
        }
        render_json_page(&columns, &rows, &values, budget(budget_bytes))
    })())
}
