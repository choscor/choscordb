use super::{convert, ffi};
use choscordb_driver_api::parse_grid_edit_value_checked;

pub fn parse_grid_edit_value_policy(
    database_type: &str,
    text: &str,
) -> ffi::ParsedGridEditValueDto {
    match parse_grid_edit_value_checked(database_type, text) {
        Ok(value) => ffi::ParsedGridEditValueDto {
            valid: true,
            cell: convert::cell(value),
            error: String::new(),
        },
        Err(error) => ffi::ParsedGridEditValueDto {
            error,
            ..Default::default()
        },
    }
}
