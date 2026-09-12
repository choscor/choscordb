use crate::{BridgeEngine, ffi, submit};
use choscordb_core::QueryPreferences;
use choscordb_driver_api::PageSize;
pub(crate) fn dto(value: QueryPreferences) -> ffi::QueryPreferencesDto {
    ffi::QueryPreferencesDto {
        version: value.version,
        page_size: value.page_size.get(),
        timeout_seconds: value.timeout_seconds,
    }
}
pub fn query_preference_limits() -> ffi::QueryPreferenceLimitsDto {
    ffi::QueryPreferenceLimitsDto {
        version: choscordb_core::QUERY_PREFERENCES_VERSION,
        min_page_size: choscordb_driver_api::MIN_PAGE_SIZE,
        max_page_size: choscordb_driver_api::MAX_PAGE_SIZE,
        default_page_size: choscordb_driver_api::DEFAULT_PAGE_SIZE,
        max_timeout_seconds: choscordb_core::MAX_QUERY_TIMEOUT_SECONDS,
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
            },
            token,
        )
        .map(|()| token)
        .map_err(|error| error.to_string())
    })
}
