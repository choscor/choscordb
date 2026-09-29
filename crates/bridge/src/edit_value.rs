use super::{convert, ffi};
use choscordb_driver_api::parse_grid_edit_value;

pub fn parse_grid_edit_value_policy(
    database_type: &str,
    text: &str,
) -> ffi::ParsedGridEditValueDto {
    let value = parse_grid_edit_value(database_type, text);
    ffi::ParsedGridEditValueDto {
        valid: value.is_some(),
        cell: value.map(convert::cell).unwrap_or_default(),
    }
}
