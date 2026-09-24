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
    struct ResultFilterDto {
        column: u32,
        operation: String,
        value_kind: String,
        value: String,
    }
    struct EditStatementDto {
        sql: String,
        params: Vec<CellDto>,
        has_expected_rows: bool,
        expected_rows: u64,
    }
    #[derive(Default)]
    struct EditColumnDto {
        name: String,
        database_type: String,
        nullable: bool,
        generated: bool,
        key: bool,
    }
    #[derive(Default)]
    struct EditTargetDto {
        qualified_name: String,
        parameter_style: String,
        columns: Vec<EditColumnDto>,
        key_columns: Vec<String>,
        reason: String,
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
        tls_client_identity: String,
        tls_credential_ref: String,
        proxy_options: String,
        proxy_credential_ref: String,
        ssh_jump_credential_refs: String,
        ssh_private_key_ref: String,
        ssh_jump_private_key_refs: String,
        ssh_options: String,
        authentication: String,
        credential_ref: String,
        ssh_credential_ref: String,
        ssh_enabled: bool,
        ssh_host: String,
        ssh_port: u16,
        ssh_user: String,
        ssh_authentication: String,
        ssh_identity_source: String,
        ssh_identity_file: String,
    }
    #[derive(Default)]
    struct SshHopCredentialDto {
        id: String,
        secret: String,
        action: String,
        has_secret: bool,
        private_key: String,
        private_key_action: String,
        has_private_key: bool,
    }
    #[derive(Default)]
    struct ProfileCredentialsDto {
        ssh_hops: Vec<SshHopCredentialDto>,
        ssh_private_key: String,
        ssh_private_key_action: String,
        has_ssh_private_key: bool,
        database: String,
        ssh: String,
        tls: String,
        proxy: String,
        database_action: String,
        ssh_action: String,
        tls_action: String,
        proxy_action: String,
        has_database: bool,
        has_ssh: bool,
        has_tls: bool,
        has_proxy: bool,
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
    struct SshHostKeyCandidateDto {
        target_kind: String,
        target_id: String,
        target_index: u32,
        original_host: String,
        hostname: String,
        port: u16,
        host_key_alias: String,
        key_type: String,
        public_key: String,
        sha256: String,
        opaque_json: String,
    }
    #[derive(Default)]
    struct BridgeEvent {
        host_key_candidates: Vec<SshHostKeyCandidateDto>,
        host_key_approval: String,
        sql_mode: String,
        has_sql_mode: bool,
        has_more_results: bool,
        has_more_metadata: bool,
        metadata_offset: u64,
        next_metadata_offset: u64,
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
        result_view_rows: u64,
        result_view_scanned_rows: u64,
        result_view_buffered_rows: u64,
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
        edit_affected_rows: Vec<u64>,
        edit_target: EditTargetDto,
        edit_source_columns: Vec<String>,
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
        fn validate_connection_profile(profile: ProfileDto) -> String;
        fn profile_save_credentials(
            engine: &mut BridgeEngine,
            profile: ProfileDto,
            credentials: ProfileCredentialsDto,
            token: u64,
        ) -> Submit;
        fn profile_test_credentials(
            engine: &mut BridgeEngine,
            profile: ProfileDto,
            credentials: ProfileCredentialsDto,
            token: u64,
        ) -> Submit;
        fn profile_connect_credentials(
            engine: &mut BridgeEngine,
            profile: ProfileDto,
            credentials: ProfileCredentialsDto,
        ) -> Submit;
        fn profile_inspect_ssh_host_keys(
            engine: &mut BridgeEngine,
            profile: ProfileDto,
            credentials: ProfileCredentialsDto,
            target_kind: &str,
            target_id: &str,
            target_index: u32,
            token: u64,
        ) -> Submit;
        fn approve_ssh_host_key(
            engine: &mut BridgeEngine,
            candidate_json: &str,
            expected_sha256: &str,
            known_hosts_path: &str,
            token: u64,
        ) -> Submit;
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
        fn profile_save_secrets(
            engine: &mut BridgeEngine,
            profile: ProfileDto,
            database_action: &str,
            database_secret: &str,
            ssh_action: &str,
            ssh_secret: &str,
            token: u64,
        ) -> Submit;
        fn profile_test_secret(
            engine: &mut BridgeEngine,
            profile: ProfileDto,
            password: &str,
            has_password: bool,
            token: u64,
        ) -> Submit;
        fn profile_test_secrets(
            engine: &mut BridgeEngine,
            profile: ProfileDto,
            database_secret: &str,
            has_database_secret: bool,
            ssh_secret: &str,
            has_ssh_secret: bool,
            token: u64,
        ) -> Submit;
        fn profile_connect_secret(
            engine: &mut BridgeEngine,
            profile: ProfileDto,
            password: &str,
            has_password: bool,
        ) -> Submit;
        fn profile_connect_secrets(
            engine: &mut BridgeEngine,
            profile: ProfileDto,
            database_secret: &str,
            has_database_secret: bool,
            ssh_secret: &str,
            has_ssh_secret: bool,
        ) -> Submit;
        fn initialization_error(engine: &BridgeEngine) -> String;
        fn ssh_askpass_exit_code() -> i32;
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
        fn apply_edit_batch(
            engine: &mut BridgeEngine,
            connection: u64,
            statements: Vec<EditStatementDto>,
            request_token: u64,
        ) -> Submit;
        fn edit_target_request(
            engine: &mut BridgeEngine,
            connection: u64,
            object: &str,
            request_token: u64,
        ) -> Submit;
        fn edit_query_request(
            engine: &mut BridgeEngine,
            connection: u64,
            sql: &str,
            result_columns: Vec<String>,
            request_token: u64,
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
        fn refresh_sql_mode(
            engine: &mut BridgeEngine,
            connection: u64,
            request_token: u64,
        ) -> Submit;
        fn next_result_set(engine: &mut BridgeEngine, query: u64) -> Submit;
        fn metadata_page_request(
            engine: &mut BridgeEngine,
            connection: u64,
            parent: &str,
            request_token: u64,
            offset: u64,
            limit: u32,
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
        fn apply_result_view(
            engine: &mut BridgeEngine,
            query: u64,
            filters: Vec<ResultFilterDto>,
            sort_column: u32,
            sort_direction: &str,
            page_size: u32,
        ) -> Submit;
        fn cancel_result_view(engine: &mut BridgeEngine, query: u64) -> Submit;
        fn clear_result_view(engine: &mut BridgeEngine, query: u64) -> Submit;
        fn start_export(
            engine: &mut BridgeEngine,
            query: u64,
            destination: &str,
            format: &str,
            table: Vec<String>,
            postgres: bool,
        ) -> Submit;
        fn start_export_dialect(
            engine: &mut BridgeEngine,
            query: u64,
            destination: &str,
            format: &str,
            table: Vec<String>,
            dialect: &str,
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
        fn sql_execution_range_mysql_mode(
            sql: &str,
            cursor: u64,
            selection_start: u64,
            selection_end: u64,
            mode: &str,
        ) -> SqlRange;
        fn sql_execution_range_mysql(
            sql: &str,
            cursor: u64,
            selection_start: u64,
            selection_end: u64,
        ) -> SqlRange;
    }
}
pub fn ssh_askpass_exit_code() -> i32 {
    choscordb_driver_api::run_ssh_askpass_if_requested().unwrap_or(-1)
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
                Arc::new(choscordb_driver_mysql::MysqlDriver::new()),
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
                proxy: None,
                proxy_secret: None,
                host: host.into(),
                port,
                database: database.into(),
                user: user.into(),
                password: Some(Secret::new(password)),
                ssh_secret: None,
                ssh_private_key: None,
                ssh_jump_secrets: Default::default(),
                ssh_jump_private_keys: Default::default(),
                ssh: None,
                tls: if verify_tls {
                    TlsMode::VerifyFull
                } else {
                    TlsMode::Disable
                },
                root_certificate: None,
                tls_identity: None,
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
fn filter_value(kind: &str, value: String) -> std::result::Result<Option<Value>, String> {
    Ok(Some(match kind {
        "" => return Ok(None),
        "boolean" => Value::Bool(value.parse().map_err(|_| "Invalid boolean filter value")?),
        "integer" => Value::Integer(value.parse().map_err(|_| "Invalid integer filter value")?),
        "real" => Value::Real(value.parse().map_err(|_| "Invalid real filter value")?),
        "decimal" => Value::Decimal(value),
        "text" => Value::Text(value),
        "date" => Value::Date(value),
        "time" => Value::Time(value),
        "timestamp" => Value::Timestamp(value),
        "uuid" => Value::Uuid(value),
        "json" => Value::Json(value),
        "binary" => {
            if !value.len().is_multiple_of(2) {
                return Err(
                    "Binary filter value must contain an even number of hexadecimal digits".into(),
                );
            }
            let bytes = (0..value.len())
                .step_by(2)
                .map(|index| {
                    u8::from_str_radix(&value[index..index + 2], 16)
                        .map_err(|_| "Invalid hexadecimal binary filter value".to_string())
                })
                .collect::<std::result::Result<Vec<_>, _>>()?;
            Value::Binary(bytes)
        }
        _ => return Err("Invalid filter value type".into()),
    }))
}
pub fn apply_result_view(
    engine: &mut BridgeEngine,
    query: u64,
    filters: Vec<ffi::ResultFilterDto>,
    sort_column: u32,
    sort_direction: &str,
    page_size: u32,
) -> ffi::Submit {
    submit(engine, |e| {
        let filters = filters
            .into_iter()
            .map(|filter| {
                let operator = match filter.operation.as_str() {
                    "contains" => choscordb_core::FilterOperator::Contains,
                    "like" => choscordb_core::FilterOperator::Like,
                    "not_like" => choscordb_core::FilterOperator::NotLike,
                    "in" => choscordb_core::FilterOperator::In,
                    "sql" => choscordb_core::FilterOperator::Sql,
                    "equals" => choscordb_core::FilterOperator::Equals,
                    "not_equals" => choscordb_core::FilterOperator::NotEquals,
                    "less_than" => choscordb_core::FilterOperator::LessThan,
                    "less_than_or_equal" => choscordb_core::FilterOperator::LessThanOrEqual,
                    "greater_than" => choscordb_core::FilterOperator::GreaterThan,
                    "greater_than_or_equal" => choscordb_core::FilterOperator::GreaterThanOrEqual,
                    "is_null" => choscordb_core::FilterOperator::IsNull,
                    "is_not_null" => choscordb_core::FilterOperator::IsNotNull,
                    _ => return Err("Invalid filter operator".to_string()),
                };
                Ok(choscordb_core::FilterCondition {
                    column: filter.column as usize,
                    operator,
                    value: if matches!(
                        operator,
                        choscordb_core::FilterOperator::In | choscordb_core::FilterOperator::Sql
                    ) {
                        Some(Value::Text(filter.value))
                    } else {
                        filter_value(&filter.value_kind, filter.value)?
                    },
                })
            })
            .collect::<std::result::Result<Vec<_>, String>>()?;
        let sort = match sort_direction {
            "" => None,
            "ascending" => Some(choscordb_core::ResultSort {
                column: sort_column as usize,
                direction: choscordb_core::SortDirection::Ascending,
            }),
            "descending" => Some(choscordb_core::ResultSort {
                column: sort_column as usize,
                direction: choscordb_core::SortDirection::Descending,
            }),
            _ => return Err("Invalid sort direction".into()),
        };
        let page_size = PageSize::new(page_size).map_err(|error| error.message)?;
        e.apply_result_view(unpack(query), filters, sort, page_size)
            .map(|()| query)
            .map_err(|error| error.to_string())
    })
}
pub fn cancel_result_view(engine: &mut BridgeEngine, query: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.cancel_result_view(unpack(query))
            .map(|()| query)
            .map_err(|error| error.to_string())
    })
}
pub fn clear_result_view(engine: &mut BridgeEngine, query: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.clear_result_view(unpack(query))
            .map(|()| query)
            .map_err(|error| error.to_string())
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
pub fn sql_execution_range_mysql(
    sql: &str,
    cursor: u64,
    selection_start: u64,
    selection_end: u64,
) -> ffi::SqlRange {
    sql_execution_range_mysql_mode(sql, cursor, selection_start, selection_end, "")
}

pub fn sql_execution_range_mysql_mode(
    sql: &str,
    cursor: u64,
    selection_start: u64,
    selection_end: u64,
    mode: &str,
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
        let Some(range) = choscordb_sql_language::execution_range_mysql_with_mode(
            sql,
            cursor,
            selection,
            choscordb_sql_language::MysqlSqlMode::from_sql_mode(mode),
        ) else {
            return ffi::SqlRange::default();
        };
        ffi::SqlRange {
            valid: true,
            start: range.start as u64,
            end: range.end as u64,
            confirmation_required: choscordb_sql_language::classify_mysql_with_mode(
                &sql[range.clone()],
                choscordb_sql_language::MysqlSqlMode::from_sql_mode(mode),
            ) == choscordb_sql_language::Safety::ConfirmationRequired,
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
    start_export_dialect(
        engine,
        query,
        destination,
        format,
        table,
        if postgres { "postgres" } else { "sqlite" },
    )
}

pub fn start_export_dialect(
    engine: &mut BridgeEngine,
    query: u64,
    destination: &str,
    format: &str,
    table: Vec<String>,
    dialect: &str,
) -> ffi::Submit {
    submit(engine, |e| {
        let dialect = match dialect {
            "sqlite" => choscordb_core::SqlDialect::Sqlite,
            "postgres" => choscordb_core::SqlDialect::Postgres,
            "mysql" => choscordb_core::SqlDialect::Mysql,
            _ => return Err("Unknown SQL dialect".into()),
        };
        let format = match format {
            "csv" => choscordb_core::ExportFormat::Csv,
            "json" => choscordb_core::ExportFormat::Json,
            "jsonl" => choscordb_core::ExportFormat::JsonLines,
            "sql" => choscordb_core::ExportFormat::SqlInsert { table, dialect },
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

fn proxy_options(
    value: &str,
) -> std::result::Result<Option<choscordb_driver_api::SocksProxy>, String> {
    if value.is_empty() {
        return Ok(None);
    }
    Err("SOCKS proxy connections are no longer supported".into())
}

fn database_authentication(
    value: &str,
) -> std::result::Result<choscordb_driver_api::DatabaseAuthentication, String> {
    if value.is_empty() {
        return Ok(Default::default());
    }
    if value.len() > 64 * 1024 {
        return Err("Authentication settings exceed limit".into());
    }
    serde_json::from_str(value).map_err(|_| "Invalid database authentication settings".into())
}
pub fn validate_connection_profile(dto: ffi::ProfileDto) -> String {
    profile(dto).err().unwrap_or_default()
}

fn ssh_options(value: &str) -> std::result::Result<choscordb_driver_api::SshOptions, String> {
    if value.is_empty() {
        return Ok(Default::default());
    }
    if value.len() > 16 * 1024 {
        return Err("SSH options exceed limit".into());
    }
    let options: choscordb_driver_api::SshOptions =
        serde_json::from_str(value).map_err(|_| "Invalid SSH options".to_string())?;
    if options != choscordb_driver_api::SshOptions::default() {
        return Err("Advanced SSH tunnel settings are no longer supported".into());
    }
    Ok(options)
}
fn ssh_tunnel(
    dto: &ffi::ProfileDto,
) -> std::result::Result<Option<choscordb_driver_api::SshTunnel>, String> {
    use choscordb_driver_api::{SshAuthentication, SshTunnel};
    if !dto.ssh_enabled {
        return Ok(None);
    }
    Ok(Some(SshTunnel {
        options: Box::new(ssh_options(&dto.ssh_options)?),
        host: dto.ssh_host.clone(),
        port: dto.ssh_port,
        user: dto.ssh_user.clone(),
        authentication: match dto.ssh_authentication.as_str() {
            "agent" => SshAuthentication::Agent,
            "public_key" => SshAuthentication::PublicKey,
            "password" => SshAuthentication::Password,
            "" if dto.ssh_identity_file.is_empty() => SshAuthentication::Agent,
            "" => SshAuthentication::PublicKey,
            _ => return Err("Invalid SSH authentication method".into()),
        },
        identity_source: match dto.ssh_identity_source.as_str() {
            "" | "file" => choscordb_driver_api::SshIdentitySource::File,
            "inline" => choscordb_driver_api::SshIdentitySource::Inline,
            _ => return Err("Invalid SSH identity source".into()),
        },
        identity_file: (!dto.ssh_identity_file.is_empty()).then(|| dto.ssh_identity_file.clone()),
    }))
}
fn profile(dto: ffi::ProfileDto) -> std::result::Result<choscordb_core::ConnectionProfile, String> {
    use choscordb_core::{ConnectionProfile, PostgresTls, ProfileConfiguration};
    let ssh = if dto.driver == "sqlite" {
        None
    } else {
        ssh_tunnel(&dto)?
    };
    let configuration = match dto.driver.as_str() {
        "sqlite" => ProfileConfiguration::Sqlite {
            path: dto.path,
            read_only: dto.read_only,
        },
        "mysql" => ProfileConfiguration::Mysql {
            proxy: proxy_options(&dto.proxy_options)?,
            ssh,
            host: dto.host,
            port: dto.port,
            database: dto.database,
            user: dto.user,
            tls: PostgresTls {
                client_identity_path: (!dto.tls_client_identity.is_empty())
                    .then_some(dto.tls_client_identity),
                mode: match dto.tls.as_str() {
                    "disable" => TlsMode::Disable,
                    "verify_full" => TlsMode::VerifyFull,
                    "verify_ca" => TlsMode::VerifyCa,
                    "require" => TlsMode::Require,
                    "prefer" => TlsMode::Prefer,
                    _ => return Err("Invalid TLS mode".into()),
                },
                root_certificate_path: (!dto.root_certificate.is_empty())
                    .then_some(dto.root_certificate),
            },
        },
        "postgres" => ProfileConfiguration::Postgres {
            proxy: proxy_options(&dto.proxy_options)?,
            host: dto.host,
            port: dto.port,
            database: dto.database,
            user: dto.user,
            ssh,
            tls: PostgresTls {
                client_identity_path: (!dto.tls_client_identity.is_empty())
                    .then_some(dto.tls_client_identity),
                mode: match dto.tls.as_str() {
                    "disable" => TlsMode::Disable,
                    "verify_full" => TlsMode::VerifyFull,
                    "verify_ca" => TlsMode::VerifyCa,
                    "require" => TlsMode::Require,
                    "prefer" => TlsMode::Prefer,
                    _ => return Err("Invalid TLS mode".into()),
                },
                root_certificate_path: (!dto.root_certificate.is_empty())
                    .then_some(dto.root_certificate),
            },
        },
        _ => return Err("Unknown profile driver".into()),
    };
    let mut profile = ConnectionProfile {
        authentication: database_authentication(&dto.authentication)?,
        id: dto.id,
        name: dto.name,
        group_id: (!dto.group_id.is_empty()).then_some(dto.group_id),
        configuration,
        credential_ref: (!dto.credential_ref.is_empty()).then_some(dto.credential_ref),
        ssh_credential_ref: (!dto.ssh_credential_ref.is_empty()).then_some(dto.ssh_credential_ref),
        ssh_private_key_ref: (!dto.ssh_private_key_ref.is_empty())
            .then_some(dto.ssh_private_key_ref),
        tls_credential_ref: (!dto.tls_credential_ref.is_empty()).then_some(dto.tls_credential_ref),
        proxy_credential_ref: (!dto.proxy_credential_ref.is_empty())
            .then_some(dto.proxy_credential_ref),
        ssh_jump_credential_refs: if dto.ssh_jump_credential_refs.is_empty() {
            Default::default()
        } else {
            if dto.ssh_jump_credential_refs.len() > 16 * 1024 {
                return Err("Invalid SSH hop credential references".into());
            }
            serde_json::from_str(&dto.ssh_jump_credential_refs)
                .map_err(|_| "Invalid SSH hop credential references".to_string())?
        },
        ssh_jump_private_key_refs: if dto.ssh_jump_private_key_refs.is_empty() {
            Default::default()
        } else {
            if dto.ssh_jump_private_key_refs.len() > 16 * 1024 {
                return Err("Invalid SSH inline key references".into());
            }
            serde_json::from_str(&dto.ssh_jump_private_key_refs)
                .map_err(|_| "Invalid SSH inline key references".to_string())?
        },
    };
    let active_hops = match &profile.configuration {
        ProfileConfiguration::Postgres { ssh: Some(ssh), .. }
        | ProfileConfiguration::Mysql { ssh: Some(ssh), .. } => ssh
            .options
            .jump_hosts
            .iter()
            .filter(|hop| hop.authentication.uses_secret())
            .filter_map(|hop| hop.id.as_deref())
            .collect::<std::collections::BTreeSet<_>>(),
        _ => std::collections::BTreeSet::new(),
    };
    profile
        .ssh_jump_credential_refs
        .retain(|id, _| active_hops.contains(id.as_str()));
    let inline_hops = match &profile.configuration {
        ProfileConfiguration::Postgres { ssh: Some(ssh), .. }
        | ProfileConfiguration::Mysql { ssh: Some(ssh), .. } => ssh
            .options
            .jump_hosts
            .iter()
            .filter(|hop| {
                hop.authentication == choscordb_driver_api::SshJumpAuthentication::PublicKey
                    && hop.identity_source == choscordb_driver_api::SshIdentitySource::Inline
            })
            .filter_map(|hop| hop.id.as_deref())
            .collect::<std::collections::BTreeSet<_>>(),
        _ => std::collections::BTreeSet::new(),
    };
    profile
        .ssh_jump_private_key_refs
        .retain(|id, _| inline_hops.contains(id.as_str()));
    let target_inline = match &profile.configuration {
        ProfileConfiguration::Postgres { ssh: Some(ssh), .. }
        | ProfileConfiguration::Mysql { ssh: Some(ssh), .. } => {
            ssh.authentication == choscordb_driver_api::SshAuthentication::PublicKey
                && ssh.identity_source == choscordb_driver_api::SshIdentitySource::Inline
        }
        _ => false,
    };
    if !target_inline {
        profile.ssh_private_key_ref = None;
    }
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
        e.profile_save(profile_for_save(dto)?, token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
fn profile_for_save(
    mut dto: ffi::ProfileDto,
) -> std::result::Result<choscordb_core::ConnectionProfile, String> {
    // The metadata worker recovers only references owned by the existing profile.
    // Draft references can be stale after deleting or changing a hop.
    dto.ssh_jump_credential_refs.clear();
    dto.ssh_private_key_ref.clear();
    dto.ssh_jump_private_key_refs.clear();
    profile(dto)
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
    if password.len() > 16 * 1024 {
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
    profile_save_secrets(engine, dto, action, password, "keep", "", token)
}
fn credential_update(
    action: &str,
    value: &str,
) -> std::result::Result<choscordb_core::CredentialUpdate, String> {
    match action {
        "keep" => Ok(choscordb_core::CredentialUpdate::Keep),
        "clear" => Ok(choscordb_core::CredentialUpdate::Clear),
        "replace" => Ok(choscordb_core::CredentialUpdate::Replace(
            secret(value, true)?.expect("present secret"),
        )),
        _ => Err("Invalid credential update".into()),
    }
}
pub fn profile_save_secrets(
    engine: &mut BridgeEngine,
    dto: ffi::ProfileDto,
    database_action: &str,
    database_secret: &str,
    ssh_action: &str,
    ssh_secret: &str,
    token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        let updates = choscordb_core::CredentialUpdates {
            ssh_private_key: choscordb_core::CredentialUpdate::Keep,
            ssh_jump_private_keys: Default::default(),
            ssh_jumps: Default::default(),
            database: credential_update(database_action, database_secret)?,
            ssh: credential_update(ssh_action, ssh_secret)?,
            tls: choscordb_core::CredentialUpdate::Keep,
            proxy: choscordb_core::CredentialUpdate::Keep,
        };
        e.profile_save_with_secrets(profile_for_save(dto)?, updates, token)
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
    profile_test_secrets(engine, dto, password, has_password, "", false, token)
}
pub fn profile_test_secrets(
    engine: &mut BridgeEngine,
    dto: ffi::ProfileDto,
    database_secret: &str,
    has_database_secret: bool,
    ssh_secret: &str,
    has_ssh_secret: bool,
    token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        e.test_profile_with_secrets(
            profile(dto)?,
            choscordb_core::ProfileSecrets {
                ssh_private_key: None,
                ssh_jump_private_keys: Default::default(),
                ssh_jumps: Default::default(),
                database: secret(database_secret, has_database_secret)?,
                ssh: secret(ssh_secret, has_ssh_secret)?,
                tls: None,
                proxy: None,
            },
            token,
        )
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
    profile_connect_secrets(engine, dto, password, has_password, "", false)
}
pub fn profile_connect_secrets(
    engine: &mut BridgeEngine,
    dto: ffi::ProfileDto,
    database_secret: &str,
    has_database_secret: bool,
    ssh_secret: &str,
    has_ssh_secret: bool,
) -> ffi::Submit {
    submit(engine, |e| {
        e.connect_profile_with_secrets(
            profile(dto)?,
            choscordb_core::ProfileSecrets {
                ssh_private_key: None,
                ssh_jump_private_keys: Default::default(),
                ssh_jumps: Default::default(),
                database: secret(database_secret, has_database_secret)?,
                ssh: secret(ssh_secret, has_ssh_secret)?,
                tls: None,
                proxy: None,
            },
        )
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
pub fn apply_edit_batch(
    engine: &mut BridgeEngine,
    connection: u64,
    statements: Vec<ffi::EditStatementDto>,
    request_token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        let statements = statements
            .into_iter()
            .map(|statement| {
                let params = statement
                    .params
                    .into_iter()
                    .map(|cell| match cell.kind.as_str() {
                        "null" => Ok(Value::Null),
                        "boolean" => Ok(Value::Bool(cell.boolean)),
                        "integer" => Ok(Value::Integer(cell.integer)),
                        "real" => Ok(Value::Real(cell.real)),
                        "decimal" => Ok(Value::Decimal(cell.text)),
                        "text" => Ok(Value::Text(cell.text)),
                        "date" => Ok(Value::Date(cell.text)),
                        "time" => Ok(Value::Time(cell.text)),
                        "timestamp" => Ok(Value::Timestamp(cell.text)),
                        "uuid" => Ok(Value::Uuid(cell.text)),
                        "json" => Ok(Value::Json(cell.text)),
                        "binary" => Ok(Value::Binary(cell.bytes)),
                        _ => Err("Invalid edit value".to_string()),
                    })
                    .collect::<std::result::Result<Vec<_>, _>>()?;
                Ok(EditStatement {
                    sql: statement.sql,
                    params,
                    expected_rows: statement
                        .has_expected_rows
                        .then_some(statement.expected_rows),
                })
            })
            .collect::<std::result::Result<Vec<_>, String>>()?;
        e.apply_edit_batch(unpack(connection), EditBatch { statements }, request_token)
            .map(|()| connection)
            .map_err(|error| error.to_string())
    })
}
pub fn edit_target_request(
    engine: &mut BridgeEngine,
    connection: u64,
    object: &str,
    request_token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        e.edit_target_request(unpack(connection), ObjectId(object.into()), request_token)
            .map(|()| connection)
            .map_err(|error| error.to_string())
    })
}
pub fn edit_query_request(
    engine: &mut BridgeEngine,
    connection: u64,
    sql: &str,
    result_columns: Vec<String>,
    request_token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        e.edit_query_request(
            unpack(connection),
            sql.into(),
            result_columns,
            request_token,
        )
        .map(|()| connection)
        .map_err(|error| error.to_string())
    })
}

pub fn next_result_set(engine: &mut BridgeEngine, query: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.next_result_set(unpack(query))
            .map(|_| query)
            .map_err(|e| e.to_string())
    })
}
pub fn metadata_page_request(
    engine: &mut BridgeEngine,
    connection: u64,
    parent: &str,
    request_token: u64,
    offset: u64,
    limit: u32,
) -> ffi::Submit {
    submit(engine, |e| {
        e.load_metadata_page(
            unpack(connection),
            (!parent.is_empty()).then(|| ObjectId(parent.into())),
            request_token,
            offset,
            limit,
        )
        .map(|_| connection)
        .map_err(|e| e.to_string())
    })
}

pub fn refresh_sql_mode(
    engine: &mut BridgeEngine,
    connection: u64,
    request_token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        e.refresh_sql_mode(unpack(connection), request_token)
            .map(|_| connection)
            .map_err(|e| e.to_string())
    })
}

// Own each incoming secret immediately, including paths where engine/profile
// validation fails before a credential is consumed by the core.
struct ProtectedCredentials {
    ssh_hops: Vec<ProtectedSshHop>,
    ssh_private_key: zeroize::Zeroizing<String>,
    ssh_private_key_action: String,
    has_ssh_private_key: bool,
    database: zeroize::Zeroizing<String>,
    ssh: zeroize::Zeroizing<String>,
    tls: zeroize::Zeroizing<String>,
    proxy: zeroize::Zeroizing<String>,
    database_action: String,
    ssh_action: String,
    tls_action: String,
    proxy_action: String,
    has_database: bool,
    has_ssh: bool,
    has_tls: bool,
    has_proxy: bool,
}
struct ProtectedSshHop {
    id: String,
    secret: zeroize::Zeroizing<String>,
    action: String,
    has_secret: bool,
    private_key: zeroize::Zeroizing<String>,
    private_key_action: String,
    has_private_key: bool,
}
impl From<ffi::ProfileCredentialsDto> for ProtectedCredentials {
    fn from(value: ffi::ProfileCredentialsDto) -> Self {
        Self {
            ssh_hops: value
                .ssh_hops
                .into_iter()
                .map(|hop| ProtectedSshHop {
                    id: hop.id,
                    secret: zeroize::Zeroizing::new(hop.secret),
                    action: hop.action,
                    has_secret: hop.has_secret,
                    private_key: zeroize::Zeroizing::new(hop.private_key),
                    private_key_action: hop.private_key_action,
                    has_private_key: hop.has_private_key,
                })
                .collect(),
            database: zeroize::Zeroizing::new(value.database),
            ssh: zeroize::Zeroizing::new(value.ssh),
            ssh_private_key: zeroize::Zeroizing::new(value.ssh_private_key),
            ssh_private_key_action: value.ssh_private_key_action,
            has_ssh_private_key: value.has_ssh_private_key,
            tls: zeroize::Zeroizing::new(value.tls),
            proxy: zeroize::Zeroizing::new(value.proxy),
            database_action: value.database_action,
            ssh_action: value.ssh_action,
            tls_action: value.tls_action,
            proxy_action: value.proxy_action,
            has_database: value.has_database,
            has_ssh: value.has_ssh,
            has_tls: value.has_tls,
            has_proxy: value.has_proxy,
        }
    }
}
fn owned_secret(
    value: zeroize::Zeroizing<String>,
    present: bool,
) -> std::result::Result<Option<Secret>, String> {
    owned_secret_with_limit(value, present, 16 * 1024)
}
fn owned_private_key_secret(
    value: zeroize::Zeroizing<String>,
    present: bool,
) -> std::result::Result<Option<Secret>, String> {
    owned_secret_with_limit(value, present, choscordb_credentials::MAX_SECRET_BYTES)
}
fn owned_secret_with_limit(
    mut value: zeroize::Zeroizing<String>,
    present: bool,
    limit: usize,
) -> std::result::Result<Option<Secret>, String> {
    if !present {
        return Ok(None);
    }
    if value.len() > limit {
        return Err("Credential exceeds the supported size".into());
    }
    Ok(Some(Secret::new(std::mem::take(&mut *value))))
}
fn owned_update(
    action: &str,
    value: zeroize::Zeroizing<String>,
) -> std::result::Result<choscordb_core::CredentialUpdate, String> {
    use choscordb_core::CredentialUpdate;
    match action {
        "" | "keep" => Ok(CredentialUpdate::Keep),
        "clear" => Ok(CredentialUpdate::Clear),
        "replace" => Ok(CredentialUpdate::Replace(
            owned_secret(value, true)?.expect("present secret"),
        )),
        _ => Err("Invalid credential update".into()),
    }
}
fn owned_private_key_update(
    action: &str,
    value: zeroize::Zeroizing<String>,
) -> std::result::Result<choscordb_core::CredentialUpdate, String> {
    use choscordb_core::CredentialUpdate;
    match action {
        "" | "keep" => Ok(CredentialUpdate::Keep),
        "clear" => Ok(CredentialUpdate::Clear),
        "replace" => Ok(CredentialUpdate::Replace(
            owned_private_key_secret(value, true)?.expect("present private key"),
        )),
        _ => Err("Invalid credential update".into()),
    }
}
fn hop_updates(hops: Vec<ProtectedSshHop>) -> std::result::Result<HopUpdates, String> {
    if hops.len() > 5 {
        return Err("Too many SSH hop credentials".into());
    }
    let mut updates = std::collections::BTreeMap::new();
    let mut key_updates = std::collections::BTreeMap::new();
    for hop in hops {
        let update = owned_update(&hop.action, hop.secret)?;
        let key_update = owned_private_key_update(&hop.private_key_action, hop.private_key)?;
        if updates.insert(hop.id.clone(), update).is_some() {
            return Err("Duplicate SSH hop credential".into());
        }
        key_updates.insert(hop.id, key_update);
    }
    Ok((updates, key_updates))
}
type HopUpdates = (
    std::collections::BTreeMap<String, choscordb_core::CredentialUpdate>,
    std::collections::BTreeMap<String, choscordb_core::CredentialUpdate>,
);
fn hop_secrets(hops: Vec<ProtectedSshHop>) -> std::result::Result<HopSecrets, String> {
    if hops.len() > 5 {
        return Err("Too many SSH hop credentials".into());
    }
    let mut secrets = std::collections::BTreeMap::new();
    let mut keys = std::collections::BTreeMap::new();
    let mut ids = std::collections::BTreeSet::new();
    for hop in hops {
        if !ids.insert(hop.id.clone()) {
            return Err("Duplicate SSH hop credential".into());
        }
        if let Some(secret) = owned_secret(hop.secret, hop.has_secret)? {
            secrets.insert(hop.id.clone(), secret);
        }
        if let Some(key) = owned_private_key_secret(hop.private_key, hop.has_private_key)? {
            keys.insert(hop.id, key);
        }
    }
    Ok((secrets, keys))
}
type HopSecrets = (
    std::collections::BTreeMap<String, Secret>,
    std::collections::BTreeMap<String, Secret>,
);
pub fn profile_save_credentials(
    engine: &mut BridgeEngine,
    dto: ffi::ProfileDto,
    credentials: ffi::ProfileCredentialsDto,
    token: u64,
) -> ffi::Submit {
    let credentials = ProtectedCredentials::from(credentials);
    submit(engine, |e| {
        let (ssh_jumps, ssh_jump_private_keys) = hop_updates(credentials.ssh_hops)?;
        let updates = choscordb_core::CredentialUpdates {
            ssh_private_key: owned_private_key_update(
                &credentials.ssh_private_key_action,
                credentials.ssh_private_key,
            )?,
            ssh_jump_private_keys,
            ssh_jumps,
            database: owned_update(&credentials.database_action, credentials.database)?,
            ssh: owned_update(&credentials.ssh_action, credentials.ssh)?,
            tls: owned_update(&credentials.tls_action, credentials.tls)?,
            proxy: owned_update(&credentials.proxy_action, credentials.proxy)?,
        };
        e.profile_save_with_secrets(profile(dto)?, updates, token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
pub fn profile_test_credentials(
    engine: &mut BridgeEngine,
    dto: ffi::ProfileDto,
    credentials: ffi::ProfileCredentialsDto,
    token: u64,
) -> ffi::Submit {
    let credentials = ProtectedCredentials::from(credentials);
    submit(engine, |e| {
        let (ssh_jumps, ssh_jump_private_keys) = hop_secrets(credentials.ssh_hops)?;
        let secrets = choscordb_core::ProfileSecrets {
            ssh_private_key: owned_private_key_secret(
                credentials.ssh_private_key,
                credentials.has_ssh_private_key,
            )?,
            ssh_jump_private_keys,
            ssh_jumps,
            database: owned_secret(credentials.database, credentials.has_database)?,
            ssh: owned_secret(credentials.ssh, credentials.has_ssh)?,
            tls: owned_secret(credentials.tls, credentials.has_tls)?,
            proxy: owned_secret(credentials.proxy, credentials.has_proxy)?,
        };
        e.test_profile_with_secrets(profile(dto)?, secrets, token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
pub fn profile_connect_credentials(
    engine: &mut BridgeEngine,
    dto: ffi::ProfileDto,
    credentials: ffi::ProfileCredentialsDto,
) -> ffi::Submit {
    let credentials = ProtectedCredentials::from(credentials);
    submit(engine, |e| {
        let (ssh_jumps, ssh_jump_private_keys) = hop_secrets(credentials.ssh_hops)?;
        let secrets = choscordb_core::ProfileSecrets {
            ssh_private_key: owned_private_key_secret(
                credentials.ssh_private_key,
                credentials.has_ssh_private_key,
            )?,
            ssh_jump_private_keys,
            ssh_jumps,
            database: owned_secret(credentials.database, credentials.has_database)?,
            ssh: owned_secret(credentials.ssh, credentials.has_ssh)?,
            tls: owned_secret(credentials.tls, credentials.has_tls)?,
            proxy: owned_secret(credentials.proxy, credentials.has_proxy)?,
        };
        e.connect_profile_with_secrets(profile(dto)?, secrets)
            .map(pack)
            .map_err(|e| e.to_string())
    })
}

pub fn profile_inspect_ssh_host_keys(
    engine: &mut BridgeEngine,
    dto: ffi::ProfileDto,
    credentials: ffi::ProfileCredentialsDto,
    target_kind: &str,
    target_id: &str,
    target_index: u32,
    token: u64,
) -> ffi::Submit {
    let credentials = ProtectedCredentials::from(credentials);
    submit(engine, |e| {
        let target = match target_kind {
            "target" => choscordb_driver_api::SshHostKeyTarget::Target,
            "jump" => choscordb_driver_api::SshHostKeyTarget::Jump(target_id.into()),
            "jump_index" => choscordb_driver_api::SshHostKeyTarget::JumpIndex(
                usize::try_from(target_index).map_err(|_| "Invalid SSH hop index")?,
            ),
            _ => return Err("Invalid SSH host key target".into()),
        };
        if credentials.has_database
            || credentials.has_ssh
            || credentials.has_ssh_private_key
            || credentials.has_tls
            || credentials.has_proxy
            || !credentials.database.is_empty()
            || !credentials.ssh.is_empty()
            || !credentials.ssh_private_key.is_empty()
            || !credentials.tls.is_empty()
            || !credentials.proxy.is_empty()
        {
            return Err("SSH host inspection accepts only preceding-hop credentials".into());
        }
        let (ssh_jumps, ssh_jump_private_keys) = hop_secrets(credentials.ssh_hops)?;
        let secrets = choscordb_core::ProfileSecrets {
            ssh_jumps,
            ssh_jump_private_keys,
            ..Default::default()
        };
        e.inspect_profile_ssh_host_keys(profile(dto)?, secrets, target, token)
            .map(|()| token)
            .map_err(|error| error.to_string())
    })
}

pub fn approve_ssh_host_key(
    engine: &mut BridgeEngine,
    candidate_json: &str,
    expected_sha256: &str,
    known_hosts_path: &str,
    token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        if candidate_json.len() > 32 * 1024
            || expected_sha256.len() > 128
            || known_hosts_path.is_empty()
            || known_hosts_path.len() > 16 * 1024
            || !std::path::Path::new(known_hosts_path).is_absolute()
            || known_hosts_path.starts_with('~')
            || known_hosts_path.contains(['\0', '%', '$', '"', '\'', '\\', '\n', '\r'])
        {
            return Err("Invalid SSH host key approval".into());
        }
        let candidate: choscordb_driver_api::SshHostKeyCandidate =
            serde_json::from_str(candidate_json).map_err(|_| "Invalid SSH host key candidate")?;
        e.approve_ssh_host_key(
            candidate,
            expected_sha256.into(),
            std::path::PathBuf::from(known_hosts_path),
            token,
        )
        .map(|()| token)
        .map_err(|error| error.to_string())
    })
}
