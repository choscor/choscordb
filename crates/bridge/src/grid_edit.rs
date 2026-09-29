//! Typed transport for grid edit rules owned by driver-api.
use crate::{convert, ffi};
use choscordb_driver_api::{
    GridEditColumn, GridEditDriver, GridEditError, GridEditRequest, GridEditRow, grid_editability,
    plan_grid_edits,
};

fn request(source: ffi::GridEditRequestDto) -> Result<GridEditRequest, GridEditError> {
    let driver = match source.driver.as_str() {
        "sqlite" => GridEditDriver::Sqlite,
        "postgres" | "postgresql" => GridEditDriver::Postgres,
        "mysql" => GridEditDriver::Mysql,
        _ => return Err(GridEditError::UnsupportedTarget),
    };
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
