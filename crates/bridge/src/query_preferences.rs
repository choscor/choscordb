use crate::{BridgeEngine, ffi, submit};
use choscordb_core::{QueryPreferences, RunAllStep};
use choscordb_driver_api::PageSize;
pub(crate) fn dto(value: QueryPreferences) -> ffi::QueryPreferencesDto {
    ffi::QueryPreferencesDto {
        version: value.version,
        page_size: value.page_size.get(),
        timeout_seconds: value.timeout_seconds,
        connection_timeout_seconds: value.connection_timeout_seconds,
        show_system_schemas: value.show_system_schemas,
        stop_on_first_error: value.stop_on_first_error,
    }
}
pub fn query_preference_limits() -> ffi::QueryPreferenceLimitsDto {
    ffi::QueryPreferenceLimitsDto {
        version: choscordb_core::QUERY_PREFERENCES_VERSION,
        min_page_size: choscordb_driver_api::MIN_PAGE_SIZE,
        max_page_size: choscordb_driver_api::MAX_PAGE_SIZE,
        default_page_size: choscordb_driver_api::DEFAULT_PAGE_SIZE,
        max_timeout_seconds: choscordb_core::MAX_QUERY_TIMEOUT_SECONDS,
        default_connection_timeout_seconds: choscordb_core::DEFAULT_CONNECTION_TIMEOUT_SECONDS,
        max_connection_timeout_seconds: choscordb_core::MAX_CONNECTION_TIMEOUT_SECONDS,
        default_stop_on_first_error: choscordb_core::DEFAULT_STOP_ON_FIRST_ERROR,
    }
}
pub fn run_all_step_policy(stop_on_first_error: bool, failed: bool) -> ffi::RunAllStepDto {
    match choscordb_core::run_all_step(stop_on_first_error, failed) {
        RunAllStep::Next => ffi::RunAllStepDto {
            stop: false,
            message: String::new(),
        },
        RunAllStep::Stop { message } => ffi::RunAllStepDto {
            stop: true,
            message: message.into(),
        },
    }
}
pub fn query_preferences_get(engine: &mut BridgeEngine, token: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.query_preferences_get(token)
            .map(|()| token)
            .map_err(|error| error.to_string())
    })
}
pub fn query_preferences_set(
    engine: &mut BridgeEngine,
    value: ffi::QueryPreferencesDto,
    token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        let page_size = PageSize::new(value.page_size).map_err(|error| error.to_string())?;
        e.query_preferences_set(
            QueryPreferences {
                version: value.version,
                page_size,
                timeout_seconds: value.timeout_seconds,
                connection_timeout_seconds: value.connection_timeout_seconds,
                show_system_schemas: value.show_system_schemas,
                stop_on_first_error: value.stop_on_first_error,
            },
            token,
        )
        .map(|()| token)
        .map_err(|error| error.to_string())
    })
}
