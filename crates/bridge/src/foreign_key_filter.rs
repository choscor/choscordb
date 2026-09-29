use super::{convert, ffi};
use choscordb_driver_api::{foreign_key_predicate, foreign_key_value_filterable};

pub fn foreign_key_value_filterable_policy(value: ffi::CellDto) -> bool {
    convert::value(value)
        .map(|value| foreign_key_value_filterable(&value))
        .unwrap_or(false)
}

pub fn foreign_key_predicate_policy(
    target_column: &str,
    value: ffi::CellDto,
) -> ffi::ForeignKeyPredicateDto {
    let expression = convert::value(value)
        .ok()
        .and_then(|value| foreign_key_predicate(target_column, &value));
    ffi::ForeignKeyPredicateDto {
        valid: expression.is_some(),
        expression: expression.unwrap_or_default(),
    }
}
