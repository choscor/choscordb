use crate::{BridgeEngine, ffi, submit};

pub fn result_columns_context(profile_id: &str, object_id: &str) -> String {
    choscordb_core::result_columns_context(profile_id, object_id)
}

pub fn result_column_toggle(
    hidden: Vec<String>,
    name: &str,
    hide: bool,
    columns: Vec<String>,
) -> ffi::HiddenColumnsToggleDto {
    match choscordb_core::toggle_hidden_column(&hidden, name, hide, &columns) {
        Ok(hidden) => ffi::HiddenColumnsToggleDto {
            hidden,
            error: String::new(),
        },
        Err(error) => ffi::HiddenColumnsToggleDto {
            hidden,
            error: error.to_string(),
        },
    }
}

pub fn result_columns_get(engine: &mut BridgeEngine, context: &str, token: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.result_columns_get(context.to_owned(), token)
            .map(|()| token)
            .map_err(|error| error.to_string())
    })
}

pub fn result_columns_set(
    engine: &mut BridgeEngine,
    context: &str,
    hidden: Vec<String>,
    token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        e.result_columns_set(context.to_owned(), hidden, token)
            .map(|()| token)
            .map_err(|error| error.to_string())
    })
}
