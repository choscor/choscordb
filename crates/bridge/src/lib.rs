//! Typed, nonblocking CXX transport. Application policy remains in core services.
mod appearance;
pub use appearance::{appearance_layout_get, appearance_layout_reset, appearance_layout_set};
mod completion;
mod convert;
pub use completion::*;
mod preferences;
mod query_preferences;
pub use query_preferences::{
    query_preference_limits, query_preferences_get, query_preferences_set,
};
mod templates;
pub use templates::*;
mod recovery;
use choscordb_core::{Engine, EngineConfig};
use choscordb_driver_api::*;
pub use preferences::*;
pub use recovery::*;
use std::{
    panic::{AssertUnwindSafe, catch_unwind},
    sync::Arc,
    time::Duration,
};
// SAFETY: cxx generates the unsafe ABI glue for this one declarative boundary. The bridge's
// generated static assertions validate the shared layouts and signatures, while transport tests
// exercise every public message family across the boundary. Hand-written unsafe remains denied.
#[allow(
    unsafe_code,
    reason = "audited cxx::bridge expansion is the sole generated FFI boundary"
)]
#[cxx::bridge(namespace = "choscordb")]
pub mod ffi {
    #[derive(Default)]
    struct Submit {
        accepted: bool,
        id: u64,
        error: String,
    }
    #[derive(Default)]
    struct ColumnDto {
        name: String,
        database_type: String,
        has_precision: bool,
        precision: u32,
        has_scale: bool,
        scale: i32,
        timezone: String,
        nullability: i8,
    }
    #[derive(Default)]
    struct CellDto {
        kind: String,
        text: String,
        integer: i64,
        real: f64,
        boolean: bool,
        bytes: Vec<u8>,
        handle: u64,
        byte_length: u64,
        database_type: String,
    }
    #[derive(Default)]
    struct MetadataPropertyDto {
        name: String,
        value: String,
        availability: String,
        reason: String,
    }
    #[derive(Default)]
    struct MetadataDto {
        id: String,
        parent: String,
        name: String,
        qualified_name: String,
        kind: String,
        has_children: bool,
        has_column: bool,
        column: ColumnDto,
        properties: Vec<MetadataPropertyDto>,
    }
    #[derive(Default)]
    struct ProfileDto {
        id: String,
        name: String,
        group_id: String,
        driver: String,
        path: String,
        read_only: bool,
        host: String,
        port: u16,
        database: String,
        user: String,
        tls: String,
        root_certificate: String,
        credential_ref: String,
    }
    #[derive(Default)]
    struct SqlTemplateResultDto {
        valid: bool,
        sql: String,
        error: String,
    }
    struct SqlTemplateLimitsDto {
        max_bytes: u64,
        max_columns: u64,
    }
    struct SqlCompletionDto {
        label: String,
        insert_text: String,
        kind: String,
    }
    struct CompletionLimitsDto {
        max_results: u64,
        max_prefix_bytes: u64,
        max_metadata_entries: u64,
        max_metadata_bytes: u64,
        max_source_bytes: u64,
    }
    #[derive(Default)]
    struct CompletionReplyDto {
        valid: bool,
        partial: bool,
        start: u64,
        end: u64,
        items: Vec<SqlCompletionDto>,
    }
    #[derive(Default)]
    struct TextMatchDto {
        valid: bool,
        found: bool,
        wrapped: bool,
        start: u64,
        end: u64,
        error: String,
    }
    #[derive(Default)]
    struct TextReplacementDto {
        valid: bool,
        text: String,
        error: String,
        count: u64,
    }
    #[derive(Default)]
    struct ShortcutOverrideDto {
        command: String,
        sequence: String,
    }
    #[derive(Default)]
    struct QueryPreferencesDto {
        version: u32,
        page_size: u32,
        timeout_seconds: u32,
    }
    #[derive(Default)]
    struct AppearanceLayoutDto {
        version: u32,
        theme: String,
        density: String,
        accent_kind: String,
        accent: String,
        navigator_width: u32,
        editor_results_split: u16,
        history_height: u32,
        navigator_visible: bool,
        history_visible: bool,
        x: i32,
        y: i32,
        width: u32,
        height: u32,
        maximized: bool,
        has_screen_name: bool,
        screen_name: String,
    }
    struct QueryPreferenceLimitsDto {
        version: u32,
        min_page_size: u32,
        max_page_size: u32,
        default_page_size: u32,
        max_timeout_seconds: u32,
    }
    #[derive(Default)]
    struct EditorPreferencesDto {
        version: u32,
        font_family: String,
        font_size: u16,
        shortcuts: Vec<ShortcutOverrideDto>,
    }
    struct EditorPreferenceLimitsDto {
        version: u32,
        default_font_size: u16,
        min_font_size: u16,
        max_font_size: u16,
    }
    struct RecoveryLimitsDto {
        max_documents: u64,
        max_sql_bytes: u64,
        max_collection_bytes: u64,
    }
    #[derive(Default)]
    struct EditorDocumentDto {
        id: String,
        title: String,
        sql: String,
        has_profile: bool,
        profile_id: String,
        has_file: bool,
        file_path: String,
        cursor_offset: u64,
        selection_anchor: u64,
        modified: bool,
    }
    #[derive(Default)]
    struct WorkspaceTabDto {
        is_object: bool,
        document: EditorDocumentDto,
        profile_id: String,
        object_type: String,
        object_id: String,
        label: String,
        pane: u32,
    }
    #[derive(Default)]
    struct HistoryEntryDto {
        id: String,
        has_profile: bool,
        profile_id: String,
        sql: String,
        timestamp: i64,
        duration_ms: u64,
        status: String,
        has_row_count: bool,
        row_count: u64,
    }
    #[derive(Default)]
    struct HistoryPolicyDto {
        enabled: bool,
        max_age_days: u32,
        max_records: u32,
    }
    #[derive(Default)]
    struct BridgeEvent {
        has_lease: bool,
        lease_id: u64,
        reserved_bytes: u64,
        request_token: u64,
        profile_id: String,
        profiles: Vec<ProfileDto>,
        documents: Vec<EditorDocumentDto>,
        workspace_tabs: Vec<WorkspaceTabDto>,
        active_tab: u32,
        history: Vec<HistoryEntryDto>,
        history_policy: HistoryPolicyDto,
        editor_preferences: EditorPreferencesDto,
        query_preferences: QueryPreferencesDto,
        has_appearance: bool,
        appearance_layout: AppearanceLayoutDto,
        history_recorded: bool,
        object: String,
        ddl: String,
        value_handle: u64,
        query_id: u64,
        exported_rows: u64,
        exported_bytes: u64,
        chunk_offset: u64,
        total_bytes: u64,
        chunk_kind: String,
        chunk_bytes: Vec<u8>,
        kind: String,
        id: u64,
        state: String,
        error: String,
        error_kind: String,
        vendor_code: String,
        columns: Vec<ColumnDto>,
        cells: Vec<CellDto>,
        row_count: u32,
        column_count: u32,
        page_index: u64,
        first_row: u64,
        has_more: bool,
        duration_ms: u64,
        has_transaction_state: bool,
        transaction_active: bool,
        has_affected_rows: bool,
        affected_rows: u64,
        warnings: Vec<String>,
        parent: String,
        objects: Vec<MetadataDto>,
        committed: bool,
        capabilities: u64,
    }
    #[derive(Default)]
    struct CacheUsageDto {
        hits: u64,
        misses: u64,
        resident_bytes: u64,
    }
    #[derive(Default)]
    struct MemoryUsageDto {
        used_bytes: u64,
        source_bytes: u64,
        peak_bytes: u64,
    }
    #[derive(Default)]
    struct SqlRange {
        valid: bool,
        start: u64,
        end: u64,
        confirmation_required: bool,
    }
    extern "Rust" {
        type BridgeEngine;
        fn new_engine() -> Box<BridgeEngine>;
        fn new_engine_with_storage(path: &str) -> Box<BridgeEngine>;
        fn recovery_limits() -> RecoveryLimitsDto;
        fn sql_template_limits() -> SqlTemplateLimitsDto;
        fn generate_sql_template(
            kind: &str,
            qualified: &str,
            columns: Vec<String>,
        ) -> SqlTemplateResultDto;
        type CompletionCatalog;
        fn completion_limits() -> CompletionLimitsDto;
        fn completion_catalog(
            items: Vec<SqlCompletionDto>,
            partial: bool,
        ) -> Box<CompletionCatalog>;
        fn complete_sql(
            catalog: &CompletionCatalog,
            sql: &str,
            cursor: u64,
            requested: bool,
        ) -> CompletionReplyDto;
        fn sql_keyword_completions(prefix: &str) -> Vec<String>;
        fn text_find(
            source: &str,
            needle: &str,
            start: u64,
            backwards: bool,
            case_sensitive: bool,
            whole_word: bool,
        ) -> TextMatchDto;
        fn text_replace_all(
            source: &str,
            needle: &str,
            replacement: &str,
            case_sensitive: bool,
            whole_word: bool,
        ) -> TextReplacementDto;
        fn workspace_save(
            engine: &mut BridgeEngine,
            documents: Vec<EditorDocumentDto>,
            token: u64,
        ) -> Submit;
        fn workspace_restore(engine: &mut BridgeEngine, token: u64) -> Submit;
        fn workspace_tabs_save(
            engine: &mut BridgeEngine,
            tabs: Vec<WorkspaceTabDto>,
            active_tab: u32,
            token: u64,
        ) -> Submit;
        fn workspace_tabs_restore(engine: &mut BridgeEngine, token: u64) -> Submit;
        fn history_list(engine: &mut BridgeEngine, limit: u32, offset: u32, token: u64) -> Submit;
        fn query_preference_limits() -> QueryPreferenceLimitsDto;
        fn appearance_layout_get(engine: &mut BridgeEngine, token: u64) -> Submit;
        fn appearance_layout_set(
            engine: &mut BridgeEngine,
            appearance: AppearanceLayoutDto,
            token: u64,
        ) -> Submit;
        fn appearance_layout_reset(engine: &mut BridgeEngine, token: u64) -> Submit;
        fn query_preferences_get(engine: &mut BridgeEngine, token: u64) -> Submit;
        fn query_preferences_set(
            engine: &mut BridgeEngine,
            preferences: QueryPreferencesDto,
            token: u64,
        ) -> Submit;
        fn editor_preferences_get(engine: &mut BridgeEngine, token: u64) -> Submit;
        fn editor_preferences_set(
            engine: &mut BridgeEngine,
            preferences: EditorPreferencesDto,
            token: u64,
        ) -> Submit;
        fn editor_preference_limits() -> EditorPreferenceLimitsDto;
        fn history_flush(engine: &mut BridgeEngine, token: u64) -> Submit;
        fn history_clear(engine: &mut BridgeEngine, token: u64) -> Submit;
        fn history_policy_get(engine: &mut BridgeEngine, token: u64) -> Submit;
        fn history_policy_set(
            engine: &mut BridgeEngine,
            policy: HistoryPolicyDto,
            token: u64,
        ) -> Submit;
        fn profile_list(engine: &mut BridgeEngine, token: u64) -> Submit;
        fn profile_save(engine: &mut BridgeEngine, profile: ProfileDto, token: u64) -> Submit;
        fn profile_duplicate(
            engine: &mut BridgeEngine,
            source: &str,
            id: &str,
            name: &str,
            token: u64,
        ) -> Submit;
        fn profile_delete(engine: &mut BridgeEngine, id: &str, token: u64) -> Submit;
        fn profile_test(engine: &mut BridgeEngine, profile: ProfileDto, token: u64) -> Submit;
        fn profile_connect(engine: &mut BridgeEngine, profile: ProfileDto) -> Submit;
        fn profile_save_secret(
            engine: &mut BridgeEngine,
            profile: ProfileDto,
            action: &str,
            password: &str,
            token: u64,
        ) -> Submit;
        fn profile_test_secret(
            engine: &mut BridgeEngine,
            profile: ProfileDto,
            password: &str,
            has_password: bool,
            token: u64,
        ) -> Submit;
        fn profile_connect_secret(
            engine: &mut BridgeEngine,
            profile: ProfileDto,
            password: &str,
            has_password: bool,
        ) -> Submit;
        fn initialization_error(engine: &BridgeEngine) -> String;
        fn connect_sqlite(engine: &mut BridgeEngine, path: &str, read_only: bool) -> Submit;
        fn connect_postgres(
            engine: &mut BridgeEngine,
            host: &str,
            port: u16,
            database: &str,
            user: &str,
            password: &str,
            verify_tls: bool,
        ) -> Submit;
        fn execute_with_profile(
            engine: &mut BridgeEngine,
            connection: u64,
            sql: &str,
            page_size: u32,
            timeout_ms: u64,
            auto_commit: bool,
            profile_id: &str,
        ) -> Submit;
        fn open_object_data(
            engine: &mut BridgeEngine,
            connection: u64,
            object: &str,
            page_size: u32,
            timeout_ms: u64,
        ) -> Submit;
        fn execute(
            engine: &mut BridgeEngine,
            connection: u64,
            sql: &str,
            page_size: u32,
            timeout_ms: u64,
            auto_commit: bool,
        ) -> Submit;
        fn metadata_request(
            engine: &mut BridgeEngine,
            connection: u64,
            parent: &str,
            request_token: u64,
        ) -> Submit;
        fn metadata(engine: &mut BridgeEngine, connection: u64, parent: &str) -> Submit;
        fn load_value_chunk(
            engine: &mut BridgeEngine,
            query: u64,
            handle: u64,
            offset: u64,
            max_bytes: u32,
        ) -> Submit;
        fn load_value(engine: &mut BridgeEngine, query: u64, handle: u64) -> Submit;
        fn object_ddl_request(
            engine: &mut BridgeEngine,
            connection: u64,
            object: &str,
            request_token: u64,
        ) -> Submit;
        fn object_ddl(engine: &mut BridgeEngine, connection: u64, object: &str) -> Submit;
        fn fetch_page(engine: &mut BridgeEngine, query: u64, page_size: u32) -> Submit;
        fn fetch_page_at(
            engine: &mut BridgeEngine,
            query: u64,
            index: u64,
            page_size: u32,
        ) -> Submit;
        fn start_export(
            engine: &mut BridgeEngine,
            query: u64,
            destination: &str,
            format: &str,
            table: Vec<String>,
            postgres: bool,
        ) -> Submit;
        fn cancel_export(engine: &mut BridgeEngine, job: u64) -> Submit;
        fn cancel(engine: &mut BridgeEngine, query: u64) -> Submit;
        fn commit(engine: &mut BridgeEngine, connection: u64) -> Submit;
        fn rollback(engine: &mut BridgeEngine, connection: u64) -> Submit;
        fn disconnect(engine: &mut BridgeEngine, connection: u64) -> Submit;
        fn release_query(engine: &mut BridgeEngine, query: u64) -> Submit;
        fn shutdown(engine: &mut BridgeEngine) -> Submit;
        fn drain_events(engine: &mut BridgeEngine) -> Vec<BridgeEvent>;
        fn release_page_lease(engine: &mut BridgeEngine, lease: u64) -> Submit;
        fn shrink_page_lease(engine: &mut BridgeEngine, lease: u64, payload_bytes: u64) -> Submit;
        fn memory_usage(engine: &BridgeEngine) -> MemoryUsageDto;
        fn cache_usage(engine: &BridgeEngine) -> CacheUsageDto;
        fn sql_execution_range(
            sql: &str,
            cursor: u64,
            selection_start: u64,
            selection_end: u64,
        ) -> SqlRange;
    }
}
pub struct BridgeEngine {
    engine: Option<Engine>,
    error: String,
    leases: Arena<choscordb_core::PageLease>,
}
fn pack(id: Handle) -> u64 {
    (u64::from(id.generation) << 32) | u64::from(id.slot)
}
fn unpack(id: u64) -> Handle {
    Handle {
        slot: id as u32,
        generation: (id >> 32) as u32,
    }
}
pub fn new_engine() -> Box<BridgeEngine> {
    make_engine(EngineConfig::default())
}
pub fn new_engine_with_storage(path: &str) -> Box<BridgeEngine> {
    make_engine(EngineConfig {
        storage_path: Some(path.into()),
        ..Default::default()
    })
}
fn make_engine(config: EngineConfig) -> Box<BridgeEngine> {
    let result = catch_unwind(|| {
        let credentials: Arc<dyn choscordb_credentials::CredentialStore> =
            if config.storage_path.is_some() {
                Arc::new(choscordb_credentials::NativeCredentialStore::new())
            } else {
                Arc::new(choscordb_credentials::UnavailableStore)
            };
        Engine::new_with_credentials(
            config,
            vec![
                Arc::new(choscordb_driver_sqlite::SqliteDriver),
                Arc::new(choscordb_driver_postgres::PostgresDriver),
            ],
            credentials,
        )
    });
    match result {
        Ok(Ok(engine)) => Box::new(BridgeEngine {
            engine: Some(engine),
            leases: Arena::default(),
            error: String::new(),
        }),
        _ => Box::new(BridgeEngine {
            engine: None,
            leases: Arena::default(),
            error: "Application engine initialization failed".into(),
        }),
    }
}
pub fn initialization_error(engine: &BridgeEngine) -> String {
    engine.error.clone()
}
fn submit(
    engine: &mut BridgeEngine,
    f: impl FnOnce(&mut Engine) -> std::result::Result<u64, String>,
) -> ffi::Submit {
    match catch_unwind(AssertUnwindSafe(|| {
        let core = engine.engine.as_mut().ok_or_else(|| engine.error.clone())?;
        f(core)
    })) {
        Ok(Ok(id)) => ffi::Submit {
            accepted: true,
            id,
            error: String::new(),
        },
        Ok(Err(error)) => ffi::Submit {
            error,
            ..Default::default()
        },
        Err(_) => ffi::Submit {
            error: "Internal command failure".into(),
            ..Default::default()
        },
    }
}
pub fn connect_sqlite(engine: &mut BridgeEngine, path: &str, read_only: bool) -> ffi::Submit {
    submit(engine, |e| {
        e.connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: path.into(),
                read_only,
            },
        )
        .map(pack)
        .map_err(|e| e.to_string())
    })
}
#[allow(clippy::too_many_arguments)]
pub fn connect_postgres(
    engine: &mut BridgeEngine,
    host: &str,
    port: u16,
    database: &str,
    user: &str,
    password: &str,
    verify_tls: bool,
) -> ffi::Submit {
    submit(engine, |e| {
        e.connect(
            "postgres",
            ConnectionOptions::Postgres {
                host: host.into(),
                port,
                database: database.into(),
                user: user.into(),
                password: Some(Secret::new(password)),
                tls: if verify_tls {
                    TlsMode::VerifyFull
                } else {
                    TlsMode::Disable
                },
                root_certificate: None,
            },
        )
        .map(pack)
        .map_err(|e| e.to_string())
    })
}
pub fn execute(
    engine: &mut BridgeEngine,
    connection: u64,
    sql: &str,
    page_size: u32,
    timeout_ms: u64,
    auto_commit: bool,
) -> ffi::Submit {
    execute_with_profile(
        engine,
        connection,
        sql,
        page_size,
        timeout_ms,
        auto_commit,
        "",
    )
}
pub fn execute_with_profile(
    engine: &mut BridgeEngine,
    connection: u64,
    sql: &str,
    page_size: u32,
    timeout_ms: u64,
    auto_commit: bool,
    profile_id: &str,
) -> ffi::Submit {
    submit(engine, |e| {
        if profile_id.len() > 256 {
            return Err("Invalid profile identifier".into());
        }
        let page_size = PageSize::new(page_size).map_err(|e| e.message)?;
        e.execute_with_profile(
            unpack(connection),
            sql.into(),
            QueryOptions {
                page_size,
                timeout: (timeout_ms != 0).then(|| Duration::from_millis(timeout_ms)),
                auto_commit,
            },
            (!profile_id.is_empty()).then(|| profile_id.to_owned()),
        )
        .map(pack)
        .map_err(|e| e.to_string())
    })
}
pub fn metadata(engine: &mut BridgeEngine, connection: u64, parent: &str) -> ffi::Submit {
    submit(engine, |e| {
        e.load_metadata(
            unpack(connection),
            if parent.is_empty() {
                None
            } else {
                Some(ObjectId(parent.into()))
            },
        )
        .map(|()| connection)
        .map_err(|e| e.to_string())
    })
}
pub fn fetch_page(engine: &mut BridgeEngine, query: u64, page_size: u32) -> ffi::Submit {
    submit(engine, |e| {
        let size = PageSize::new(page_size).map_err(|e| e.message)?;
        e.fetch_page(unpack(query), size)
            .map(|()| query)
            .map_err(|e| e.to_string())
    })
}
pub fn fetch_page_at(
    engine: &mut BridgeEngine,
    query: u64,
    index: u64,
    page_size: u32,
) -> ffi::Submit {
    submit(engine, |e| {
        let size = PageSize::new(page_size).map_err(|e| e.message)?;
        e.fetch_page_at(unpack(query), index, size)
            .map(|()| query)
            .map_err(|e| e.to_string())
    })
}
macro_rules! command {
    ($name:ident) => {
        pub fn $name(engine: &mut BridgeEngine, id: u64) -> ffi::Submit {
            submit(engine, |e| {
                e.$name(unpack(id)).map(|()| id).map_err(|e| e.to_string())
            })
        }
    };
}
command!(cancel);
command!(commit);
command!(rollback);
command!(disconnect);
command!(release_query);
pub fn shutdown(engine: &mut BridgeEngine) -> ffi::Submit {
    submit(engine, |e| {
        e.initiate_shutdown();
        Ok(0)
    })
}
pub fn drain_events(engine: &mut BridgeEngine) -> Vec<ffi::BridgeEvent> {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(core) = engine.engine.as_mut() else {
            return vec![];
        };
        let mut events = Vec::new();
        for _ in 0..1 {
            let Some(event) = core.try_event() else {
                break;
            };
            events.push(convert::event(event, &mut engine.leases));
        }
        events
    }))
    .unwrap_or_else(|_| {
        vec![ffi::BridgeEvent {
            kind: "bridge_failed".into(),
            error: "Internal event conversion failure".into(),
            ..Default::default()
        }]
    })
}
pub fn sql_execution_range(
    sql: &str,
    cursor: u64,
    selection_start: u64,
    selection_end: u64,
) -> ffi::SqlRange {
    catch_unwind(|| {
        let Ok(cursor) = usize::try_from(cursor) else {
            return ffi::SqlRange::default();
        };
        let selection = if selection_start == selection_end {
            None
        } else {
            let (Ok(start), Ok(end)) = (
                usize::try_from(selection_start),
                usize::try_from(selection_end),
            ) else {
                return ffi::SqlRange::default();
            };
            Some(start..end)
        };
        let Some(range) = choscordb_sql_language::execution_range(sql, cursor, selection) else {
            return ffi::SqlRange::default();
        };
        ffi::SqlRange {
            valid: true,
            start: range.start as u64,
            end: range.end as u64,
            confirmation_required: choscordb_sql_language::classify(&sql[range.clone()])
                == choscordb_sql_language::Safety::ConfirmationRequired,
        }
    })
    .unwrap_or_default()
}

pub fn load_value(engine: &mut BridgeEngine, query: u64, handle: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.load_value(unpack(query), unpack(handle))
            .map(|()| query)
            .map_err(|e| e.to_string())
    })
}
pub fn object_ddl(engine: &mut BridgeEngine, connection: u64, object: &str) -> ffi::Submit {
    submit(engine, |e| {
        e.object_ddl(unpack(connection), ObjectId(object.into()))
            .map(|()| connection)
            .map_err(|e| e.to_string())
    })
}

pub fn metadata_request(
    engine: &mut BridgeEngine,
    connection: u64,
    parent: &str,
    request_token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        e.load_metadata_request(
            unpack(connection),
            if parent.is_empty() {
                None
            } else {
                Some(ObjectId(parent.into()))
            },
            request_token,
        )
        .map(|()| connection)
        .map_err(|e| e.to_string())
    })
}

pub fn release_page_lease(engine: &mut BridgeEngine, lease: u64) -> ffi::Submit {
    lease_command(|| {
        engine
            .leases
            .remove(unpack(lease))
            .ok_or_else(|| "Page lease is stale".to_string())?;
        Ok(lease)
    })
}
pub fn shrink_page_lease(engine: &mut BridgeEngine, lease: u64, payload_bytes: u64) -> ffi::Submit {
    lease_command(|| {
        let bytes = usize::try_from(payload_bytes)
            .map_err(|_| "Page allocation size exceeds platform limits".to_string())?;
        engine
            .leases
            .get_mut(unpack(lease))
            .ok_or_else(|| "Page lease is stale".to_string())?
            .shrink_to(bytes)
            .map_err(|e| e.message)?;
        Ok(lease)
    })
}
fn lease_command(f: impl FnOnce() -> std::result::Result<u64, String>) -> ffi::Submit {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(Ok(id)) => ffi::Submit {
            accepted: true,
            id,
            error: String::new(),
        },
        Ok(Err(error)) => ffi::Submit {
            error,
            ..Default::default()
        },
        Err(_) => ffi::Submit {
            error: "Page allocation bookkeeping failed".into(),
            ..Default::default()
        },
    }
}
pub fn memory_usage(engine: &BridgeEngine) -> ffi::MemoryUsageDto {
    catch_unwind(AssertUnwindSafe(|| {
        engine
            .engine
            .as_ref()
            .map(|engine| {
                let usage = engine.memory_usage();
                ffi::MemoryUsageDto {
                    used_bytes: usage.used_bytes as u64,
                    source_bytes: usage.source_bytes as u64,
                    peak_bytes: usage.peak_bytes as u64,
                }
            })
            .unwrap_or_default()
    }))
    .unwrap_or_default()
}

pub fn cache_usage(engine: &BridgeEngine) -> ffi::CacheUsageDto {
    catch_unwind(AssertUnwindSafe(|| {
        engine
            .engine
            .as_ref()
            .map(|engine| {
                let usage = engine.cache_usage();
                ffi::CacheUsageDto {
                    hits: usage.hits,
                    misses: usage.misses,
                    resident_bytes: usage.resident_bytes as u64,
                }
            })
            .unwrap_or_default()
    }))
    .unwrap_or_default()
}

pub fn load_value_chunk(
    engine: &mut BridgeEngine,
    query: u64,
    handle: u64,
    offset: u64,
    max_bytes: u32,
) -> ffi::Submit {
    submit(engine, |e| {
        e.load_value_chunk(unpack(query), unpack(handle), offset, max_bytes as usize)
            .map(|()| query)
            .map_err(|e| e.to_string())
    })
}

pub fn start_export(
    engine: &mut BridgeEngine,
    query: u64,
    destination: &str,
    format: &str,
    table: Vec<String>,
    postgres: bool,
) -> ffi::Submit {
    submit(engine, |e| {
        let format = match format {
            "csv" => choscordb_core::ExportFormat::Csv,
            "json" => choscordb_core::ExportFormat::Json,
            "jsonl" => choscordb_core::ExportFormat::JsonLines,
            "sql" => choscordb_core::ExportFormat::SqlInsert {
                table,
                dialect: if postgres {
                    choscordb_core::SqlDialect::Postgres
                } else {
                    choscordb_core::SqlDialect::Sqlite
                },
            },
            _ => return Err("Unknown export format".into()),
        };
        e.start_export(unpack(query), destination.into(), format)
            .map(pack)
            .map_err(|e| e.to_string())
    })
}
pub fn cancel_export(engine: &mut BridgeEngine, export: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.cancel_export(unpack(export))
            .map(|()| export)
            .map_err(|e| e.to_string())
    })
}

fn profile(dto: ffi::ProfileDto) -> std::result::Result<choscordb_core::ConnectionProfile, String> {
    use choscordb_core::{ConnectionProfile, PostgresTls, ProfileConfiguration};
    let configuration = match dto.driver.as_str() {
        "sqlite" => ProfileConfiguration::Sqlite {
            path: dto.path,
            read_only: dto.read_only,
        },
        "postgres" => ProfileConfiguration::Postgres {
            host: dto.host,
            port: dto.port,
            database: dto.database,
            user: dto.user,
            tls: PostgresTls {
                mode: match dto.tls.as_str() {
                    "disable" => TlsMode::Disable,
                    "verify_full" => TlsMode::VerifyFull,
                    _ => return Err("Invalid TLS mode".into()),
                },
                root_certificate_path: (!dto.root_certificate.is_empty())
                    .then_some(dto.root_certificate),
            },
        },
        _ => return Err("Unknown profile driver".into()),
    };
    let profile = ConnectionProfile {
        id: dto.id,
        name: dto.name,
        group_id: (!dto.group_id.is_empty()).then_some(dto.group_id),
        configuration,
        credential_ref: (!dto.credential_ref.is_empty()).then_some(dto.credential_ref),
    };
    profile
        .validate()
        .map_err(|_| "Invalid profile".to_string())?;
    Ok(profile)
}
pub fn profile_list(engine: &mut BridgeEngine, token: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.profile_list(token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
pub fn profile_save(engine: &mut BridgeEngine, dto: ffi::ProfileDto, token: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.profile_save(profile(dto)?, token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
pub fn profile_duplicate(
    engine: &mut BridgeEngine,
    source: &str,
    id: &str,
    name: &str,
    token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        e.profile_duplicate(source.into(), id.into(), name.into(), token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
pub fn profile_delete(engine: &mut BridgeEngine, id: &str, token: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.profile_delete(id.into(), token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}

pub fn profile_test(engine: &mut BridgeEngine, dto: ffi::ProfileDto, token: u64) -> ffi::Submit {
    profile_test_secret(engine, dto, "", false, token)
}
pub fn profile_connect(engine: &mut BridgeEngine, dto: ffi::ProfileDto) -> ffi::Submit {
    profile_connect_secret(engine, dto, "", false)
}
fn secret(password: &str, present: bool) -> std::result::Result<Option<Secret>, String> {
    if !present {
        return Ok(None);
    }
    if password.len() > choscordb_credentials::MAX_SECRET_BYTES {
        return Err("Credential exceeds the supported size".into());
    }
    Ok(Some(Secret::new(password)))
}
pub fn profile_save_secret(
    engine: &mut BridgeEngine,
    dto: ffi::ProfileDto,
    action: &str,
    password: &str,
    token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        let update = match action {
            "keep" => choscordb_core::CredentialUpdate::Keep,
            "clear" => choscordb_core::CredentialUpdate::Clear,
            "replace" => choscordb_core::CredentialUpdate::Replace(
                secret(password, true)?.expect("present secret"),
            ),
            _ => return Err("Invalid credential update".into()),
        };
        e.profile_save_with_secret(profile(dto)?, update, token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
pub fn profile_test_secret(
    engine: &mut BridgeEngine,
    dto: ffi::ProfileDto,
    password: &str,
    has_password: bool,
    token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        e.test_profile(profile(dto)?, secret(password, has_password)?, token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
pub fn profile_connect_secret(
    engine: &mut BridgeEngine,
    dto: ffi::ProfileDto,
    password: &str,
    has_password: bool,
) -> ffi::Submit {
    submit(engine, |e| {
        e.connect_profile(profile(dto)?, secret(password, has_password)?)
            .map(pack)
            .map_err(|e| e.to_string())
    })
}

/// Small, immutable keyword catalog: no worker or database access is required.
pub fn sql_keyword_completions(prefix: &str) -> Vec<String> {
    if prefix.len() > 256 {
        return Vec::new();
    }
    choscordb_sql_language::completions(prefix, &[], 100)
        .into_iter()
        .map(|c| c.insert_text)
        .collect()
}

pub fn text_find(
    source: &str,
    needle: &str,
    start: u64,
    backwards: bool,
    case_sensitive: bool,
    whole_word: bool,
) -> ffi::TextMatchDto {
    let options = choscordb_sql_language::SearchOptions {
        case_sensitive,
        whole_word,
    };
    let result = usize::try_from(start)
        .map_err(|_| choscordb_sql_language::SearchError::InvalidOffset)
        .and_then(|offset| {
            choscordb_sql_language::find(source, needle, offset, backwards, options)
        });
    match result {
        Ok(Some(m)) => ffi::TextMatchDto {
            valid: true,
            found: true,
            wrapped: m.wrapped,
            start: m.start as u64,
            end: m.end as u64,
            ..Default::default()
        },
        Ok(None) => ffi::TextMatchDto {
            valid: true,
            ..Default::default()
        },
        Err(error) => ffi::TextMatchDto {
            error: error.to_string(),
            ..Default::default()
        },
    }
}
pub fn text_replace_all(
    source: &str,
    needle: &str,
    replacement: &str,
    case_sensitive: bool,
    whole_word: bool,
) -> ffi::TextReplacementDto {
    match choscordb_sql_language::replace_all(
        source,
        needle,
        replacement,
        choscordb_sql_language::SearchOptions {
            case_sensitive,
            whole_word,
        },
    ) {
        Ok(result) => ffi::TextReplacementDto {
            valid: true,
            text: result.text,
            count: result.count,
            ..Default::default()
        },
        Err(error) => ffi::TextReplacementDto {
            error: error.to_string(),
            ..Default::default()
        },
    }
}

pub fn object_ddl_request(
    engine: &mut BridgeEngine,
    connection: u64,
    object: &str,
    request_token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        e.object_ddl_request(unpack(connection), ObjectId(object.into()), request_token)
            .map(|()| connection)
            .map_err(|e| e.to_string())
    })
}

pub fn open_object_data(
    engine: &mut BridgeEngine,
    connection: u64,
    object: &str,
    page_size: u32,
    timeout_ms: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        let page_size = PageSize::new(page_size).map_err(|e| e.message)?;
        e.open_object_data(
            unpack(connection),
            ObjectId(object.into()),
            QueryOptions {
                page_size,
                timeout: (timeout_ms != 0).then(|| Duration::from_millis(timeout_ms)),
                auto_commit: false,
            },
        )
        .map(pack)
        .map_err(|e| e.to_string())
    })
}
