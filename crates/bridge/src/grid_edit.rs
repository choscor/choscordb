use crate::{convert, ffi};
use choscordb_driver_api::{
    GridCellExpression, GridEditColumn, GridEditDriver, GridEditError, GridEditRequest,
    GridEditRow, SetValueKind, SetValueOutcome, grid_editability, grid_set_value, plan_grid_edits,
};

pub(crate) fn grid_driver(name: &str) -> Result<GridEditDriver, GridEditError> {
    match name {
        "sqlite" => Ok(GridEditDriver::Sqlite),
        "postgres" | "postgresql" => Ok(GridEditDriver::Postgres),
        "mysql" | "mariadb" => Ok(GridEditDriver::Mysql),
        _ => Err(GridEditError::UnsupportedTarget),
    }
}

pub fn grid_binary_column_policy(database_type: &str) -> bool {
    choscordb_driver_api::grid_binary_column(database_type)
}

pub fn grid_temporal_kind_policy(database_type: &str) -> String {
    choscordb_driver_api::GridTemporalKind::of(database_type)
        .map_or("", |kind| kind.code())
        .into()
}

pub fn grid_temporal_text_policy(
    kind: &str,
    picked: ffi::GridDateTimeDto,
) -> ffi::GridTemporalTextDto {
    let text = choscordb_driver_api::GridTemporalKind::from_code(kind).and_then(|kind| {
        kind.text(
            (picked.year, picked.month, picked.day),
            (picked.hour, picked.minute, picked.second),
        )
    });
    match text {
        Some(text) => ffi::GridTemporalTextDto {
            text,
            error: String::new(),
        },
        None => ffi::GridTemporalTextDto {
            text: String::new(),
            error: "The picked date or time is invalid.".into(),
        },
    }
}

pub fn set_value_kinds() -> Vec<ffi::CodeLabelDto> {
    SetValueKind::ALL
        .into_iter()
        .map(|kind| ffi::CodeLabelDto {
            code: kind.code().into(),
            label: kind.label().into(),
        })
        .collect()
}

pub fn grid_set_value_policy(request: ffi::GridSetValueRequestDto) -> ffi::GridSetValueDto {
    let outcome = SetValueKind::from_code(&request.kind)
        .ok_or_else(|| "Unknown set value choice".to_string())
        .and_then(|kind| {
            let driver = grid_driver(&request.driver).map_err(|error| error.to_string())?;
            grid_set_value(
                kind,
                driver,
                &request.database_type,
                request.inserted,
                request.nullable_known.then_some(request.nullable),
            )
            .map_err(|error| error.to_string())
        });
    match outcome {
        Ok(SetValueOutcome::Null) => ffi::GridSetValueDto {
            outcome: "null".into(),
            ..Default::default()
        },
        Ok(SetValueOutcome::Text(text)) => ffi::GridSetValueDto {
            outcome: "text".into(),
            text,
            ..Default::default()
        },
        Ok(SetValueOutcome::Expression(expression)) => ffi::GridSetValueDto {
            outcome: "expression".into(),
            expression: expression.sql().into(),
            ..Default::default()
        },
        Ok(SetValueOutcome::Omit) => ffi::GridSetValueDto {
            outcome: "omit".into(),
            ..Default::default()
        },
        Err(error) => ffi::GridSetValueDto {
            error,
            ..Default::default()
        },
    }
}

fn request(source: ffi::GridEditRequestDto) -> Result<GridEditRequest, GridEditError> {
    let driver = grid_driver(&source.driver)?;
    let columns = source
        .columns
        .into_iter()
        .map(|column| GridEditColumn {
            name: column.name,
            result_name: column.result_name,
            database_type: column.database_type,
            key: column.key,
            generated: column.generated,
            enum_source_column: column.enum_source_column,
            enum_choices: column.enum_choices,
        })
        .collect();
    let rows = source
        .rows
        .into_iter()
        .map(|row| {
            Ok(GridEditRow {
                current: row
                    .current
                    .into_iter()
                    .map(|cell| convert::value(cell).map_err(|_| GridEditError::InvalidShape))
                    .collect::<Result<_, _>>()?,
                original: row
                    .original
                    .into_iter()
                    .map(|cell| convert::value(cell).map_err(|_| GridEditError::InvalidShape))
                    .collect::<Result<_, _>>()?,
                touched: row.touched.into_iter().map(|value| value != 0).collect(),
                expressions: row
                    .expressions
                    .iter()
                    .map(|sql| {
                        if sql.is_empty() {
                            Ok(None)
                        } else {
                            GridCellExpression::from_sql(sql)
                                .map(Some)
                                .ok_or(GridEditError::InvalidShape)
                        }
                    })
                    .collect::<Result<_, _>>()?,
                inserted: row.inserted,
                deleted: row.deleted,
            })
        })
        .collect::<Result<_, GridEditError>>()?;
    Ok(GridEditRequest {
        driver,
        qualified_name: source.qualified_name,
        parameter_style: source.parameter_style,
        reason: source.reason,
        object_read_only: source.object_read_only,
        columns,
        rows,
    })
}

fn flags(values: Vec<bool>) -> Vec<u8> {
    values.into_iter().map(u8::from).collect()
}

pub fn grid_cell_kind_policies() -> Vec<ffi::GridCellKindPolicyDto> {
    use choscordb_driver_api::GridCellKind as Kind;
    [
        (Kind::Value, ffi::GridCellKind::Value),
        (Kind::Binary, ffi::GridCellKind::Binary),
        (Kind::Deferred, ffi::GridCellKind::Deferred),
        (Kind::FallbackText, ffi::GridCellKind::FallbackText),
        (Kind::Unavailable, ffi::GridCellKind::Unavailable),
    ]
    .into_iter()
    .map(|(kind, dto)| {
        let policy = kind.policy();
        ffi::GridCellKindPolicyDto {
            kind: dto,
            inline_editable: policy.inline_editable,
            blocks_row_delete: policy.blocks_row_delete,
            blocks_row_duplicate: policy.blocks_row_duplicate,
            duplicate_requires_load: policy.duplicate_requires_load,
        }
    })
    .collect()
}
pub fn grid_row_insert_error_policy(rows: u32) -> String {
    choscordb_driver_api::grid_row_insert_error(rows as usize)
        .unwrap_or_default()
        .into()
}
pub fn grid_editability_policy(source: ffi::GridEditRequestDto) -> ffi::GridEditabilityDto {
    let Ok(request) = request(source) else {
        return ffi::GridEditabilityDto::default();
    };
    let result = grid_editability(&request);
    ffi::GridEditabilityDto {
        editable: flags(result.editable),
        insert_editable: flags(result.insert_editable),
        key_columns: flags(result.key_columns),
        can_insert: result.can_insert,
        can_delete: result.can_delete,
        reason: result.reason.unwrap_or_default(),
    }
}

pub fn plan_grid_edits_policy(source: ffi::GridEditRequestDto) -> ffi::GridEditPlanDto {
    let result = request(source).and_then(|request| plan_grid_edits(&request));
    match result {
        Ok(plan) => ffi::GridEditPlanDto {
            review: choscordb_driver_api::review_text(&plan),
            statements: plan
                .statements
                .into_iter()
                .map(|planned| ffi::PlannedGridEditDto {
                    statement: ffi::EditStatementDto {
                        sql: planned.statement.sql,
                        params: planned
                            .statement
                            .params
                            .into_iter()
                            .map(convert::cell)
                            .collect(),
                        has_expected_rows: planned.statement.expected_rows.is_some(),
                        expected_rows: planned.statement.expected_rows.unwrap_or_default(),
                    },
                    parameter_types: planned.parameter_types,
                })
                .collect(),
            error: String::new(),
        },
        Err(error) => ffi::GridEditPlanDto {
            error: error.to_string(),
            ..Default::default()
        },
    }
}

pub fn grid_paste_plan(
    text: &str,
    top: u32,
    left: u32,
    rows: u32,
    columns: u32,
    grid_rows: u32,
    grid_columns: u32,
) -> ffi::GridPastePlanDto {
    let selection = choscordb_core::GridRect {
        top,
        left,
        rows,
        columns,
    };
    match choscordb_core::plan_grid_paste(text, selection, grid_rows, grid_columns) {
        Ok(cells) => ffi::GridPastePlanDto {
            error: String::new(),
            rows: cells.iter().map(|cell| cell.row).collect(),
            columns: cells.iter().map(|cell| cell.column).collect(),
            texts: cells.into_iter().map(|cell| cell.text).collect(),
        },
        Err(error) => ffi::GridPastePlanDto {
            error,
            rows: Vec::new(),
            columns: Vec::new(),
            texts: Vec::new(),
        },
    }
}
