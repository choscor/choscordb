use crate::ffi;
use choscordb_core::{ObjectAction, prepare_object_action as prepare_core_object_action};

pub fn prepare_object_action(
    driver: &str,
    kind: &str,
    object_id: &str,
    qualified_name: &str,
    new_name: &str,
    relation_subtype: &str,
    rename: bool,
) -> ffi::ObjectActionStatementDto {
    let action = if rename {
        ObjectAction::Rename
    } else {
        ObjectAction::Drop
    };
    match prepare_core_object_action(
        action,
        driver,
        kind,
        object_id,
        qualified_name,
        new_name,
        relation_subtype,
    ) {
        Ok(statement) => ffi::ObjectActionStatementDto {
            valid: true,
            sql: statement.sql,
            error: String::new(),
            new_object_id: statement.new_object_id,
            new_qualified_name: statement.new_qualified_name,
        },
        Err(error) => ffi::ObjectActionStatementDto {
            error: error.to_owned(),
            ..Default::default()
        },
    }
}

pub fn object_display_identity_policy(
    object_id: &str,
    qualified_name: &str,
) -> crate::ffi::ObjectDisplayDto {
    let (schema, name) = choscordb_core::object_display_identity(object_id, qualified_name);
    crate::ffi::ObjectDisplayDto {
        has_schema: schema.is_some(),
        schema: schema.unwrap_or_default(),
        name,
    }
}
