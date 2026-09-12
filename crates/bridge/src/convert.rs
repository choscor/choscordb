use super::{ffi, pack};
use choscordb_core::Event;
use choscordb_driver_api::*;
fn column(c: Column) -> ffi::ColumnDto {
    ffi::ColumnDto {
        name: c.name,
        database_type: c.database_type,
        has_precision: c.precision.is_some(),
        precision: c.precision.unwrap_or_default(),
        has_scale: c.scale.is_some(),
        scale: c.scale.unwrap_or_default(),
        timezone: c.timezone.unwrap_or_default(),
        nullability: c.nullable.map(i8::from).unwrap_or(-1),
    }
}
fn cell(value: Value) -> ffi::CellDto {
    let mut c = ffi::CellDto::default();
    c.kind = match value {
        Value::Null => "null",
        Value::Bool(b) => {
            c.boolean = b;
            "boolean"
        }
        Value::Integer(n) => {
            c.integer = n;
            "integer"
        }
        Value::Real(n) => {
            c.real = n;
            "real"
        }
        Value::Decimal(s) => {
            c.text = s;
            "decimal"
        }
        Value::Text(s) => {
            c.text = s;
            "text"
        }
        Value::Date(s) => {
            c.text = s;
            "date"
        }
        Value::Time(s) => {
            c.text = s;
            "time"
        }
        Value::Timestamp(s) => {
            c.text = s;
            "timestamp"
        }
        Value::Uuid(s) => {
            c.text = s;
            "uuid"
        }
        Value::Json(s) => {
            c.text = s;
            "json"
        }
        Value::Binary(bytes) => {
            c.bytes = bytes;
            "binary"
        }
        Value::Deferred {
            handle,
            byte_length,
            database_type,
        } => {
            c.handle = pack(handle);
            c.byte_length = byte_length;
            c.database_type = database_type;
            "deferred"
        }
    }
    .into();
    c
}
fn error(e: &mut ffi::BridgeEvent, error: DriverError) {
    e.error = error.message;
    e.vendor_code = error.vendor_code.unwrap_or_default();
    e.error_kind = format!("{:?}", error.kind);
}
fn capabilities(c: DriverCapabilities) -> u64 {
    [
        c.schemas,
        c.transactions,
        c.native_cancellation,
        c.server_cursors,
        c.multiple_result_sets,
        c.explain_plans,
        c.editable_results,
        c.stored_procedures,
        c.database_specific_objects,
        c.ddl,
    ]
    .into_iter()
    .enumerate()
    .fold(0, |n, (bit, on)| n | ((on as u64) << bit))
}
pub fn event(event: Event, leases: &mut Arena<choscordb_core::PageLease>) -> ffi::BridgeEvent {
    let mut transfer = None;
    let mut e = ffi::BridgeEvent::default();
    e.kind = match event {
        Event::AppearanceLayout {
            request_token,
            appearance,
        } => {
            e.request_token = request_token;
            e.has_appearance = appearance.is_some();
            e.appearance_layout =
                super::appearance::dto(appearance.unwrap_or_else(Default::default));
            "appearance_layout"
        }
        Event::QueryPreferences {
            request_token,
            preferences,
        } => {
            e.request_token = request_token;
            e.query_preferences = super::query_preferences::dto(preferences);
            "query_preferences"
        }
        Event::EditorPreferences {
            request_token,
            preferences,
        } => {
            e.request_token = request_token;
            e.editor_preferences = super::preferences::dto(preferences);
            "editor_preferences"
        }
        Event::HistoryFlushed { request_token } => {
            e.request_token = request_token;
            "history_flushed"
        }
        Event::HistoryWriteFailed { query, error: err } => {
            e.id = pack(query);
            error(&mut e, err);
            "history_write_failed"
        }
        Event::WorkspaceSaved { request_token } => {
            e.request_token = request_token;
            "workspace_saved"
        }
        Event::WorkspaceRestored {
            request_token,
            documents,
        } => {
            e.request_token = request_token;
            e.documents = documents
                .into_iter()
                .map(super::recovery::document)
                .collect();
            "workspace_restored"
        }
        Event::HistoryListed {
            request_token,
            entries,
        } => {
            e.request_token = request_token;
            e.history = entries
                .into_iter()
                .map(super::recovery::history_entry)
                .collect();
            "history_listed"
        }
        Event::HistoryCleared { request_token } => {
            e.request_token = request_token;
            "history_cleared"
        }
        Event::HistoryPolicy {
            request_token,
            policy,
        } => {
            e.request_token = request_token;
            e.history_policy = ffi::HistoryPolicyDto {
                enabled: policy.enabled,
                max_age_days: policy.max_age_days,
                max_records: policy.max_records,
            };
            "history_policy"
        }
        Event::HistoryRecorded {
            request_token,
            recorded,
        } => {
            e.request_token = request_token;
            e.history_recorded = recorded;
            "history_recorded"
        }
        Event::RecoveryFailed {
            request_token,
            error: err,
        } => {
            e.request_token = request_token;
            error(&mut e, err);
            "recovery_failed"
        }
        Event::Profiles {
            request_token,
            profiles,
        } => {
            e.request_token = request_token;
            e.profiles = profiles.into_iter().map(profile).collect();
            "profiles"
        }
        Event::ProfileSaved {
            request_token,
            profile: value,
            warning,
        } => {
            e.request_token = request_token;
            e.profiles = vec![profile(value)];
            e.warnings = warning.into_iter().collect();
            "profile_saved"
        }
        Event::ProfileDeleted {
            request_token,
            id,
            warning,
        } => {
            e.request_token = request_token;
            e.profile_id = id;
            e.warnings = warning.into_iter().collect();
            "profile_deleted"
        }
        Event::ProfileTested { request_token } => {
            e.request_token = request_token;
            "profile_tested"
        }
        Event::ProfileFailed {
            request_token,
            error: err,
        } => {
            e.request_token = request_token;
            error(&mut e, err);
            "profile_failed"
        }
        Event::ExportProgress {
            export,
            query,
            rows,
            bytes,
        } => {
            e.id = pack(export);
            e.query_id = pack(query);
            e.exported_rows = rows;
            e.exported_bytes = bytes;
            "export_progress"
        }
        Event::ExportFinished {
            export,
            query,
            rows,
            bytes,
        } => {
            e.id = pack(export);
            e.query_id = pack(query);
            e.exported_rows = rows;
            e.exported_bytes = bytes;
            "export_finished"
        }
        Event::ExportFailed {
            export,
            query,
            error: err,
        } => {
            e.id = pack(export);
            e.query_id = pack(query);
            error(&mut e, err);
            "export_failed"
        }
        Event::ValueChunk {
            query,
            handle,
            chunk,
            lease,
        } => {
            transfer = Some(lease);
            e.id = pack(query);
            e.value_handle = pack(handle);
            e.chunk_offset = chunk.offset;
            e.total_bytes = chunk.total_bytes;
            e.chunk_kind = match chunk.kind {
                DeferredKind::Text => "text",
                DeferredKind::Binary => "binary",
            }
            .into();
            e.chunk_bytes = chunk.bytes;
            "value_chunk"
        }
        Event::ValueChunkFailed {
            query,
            handle,
            offset,
            error: err,
        } => {
            e.id = pack(query);
            e.value_handle = pack(handle);
            e.chunk_offset = offset;
            error(&mut e, err);
            "value_chunk_failed"
        }
        Event::Value {
            query,
            handle,
            value,
        } => {
            e.id = pack(query);
            e.value_handle = pack(handle);
            e.cells = vec![cell(value)];
            "value"
        }
        Event::Ddl {
            connection,
            object,
            ddl,
        } => {
            e.id = pack(connection);
            e.object = object.0;
            e.ddl = ddl;
            "ddl"
        }
        Event::Connected {
            connection,
            capabilities: c,
        } => {
            e.id = pack(connection);
            e.capabilities = capabilities(c);
            "connected"
        }
        Event::Disconnected { connection } => {
            e.id = pack(connection);
            "disconnected"
        }
        Event::ConnectionFailed {
            connection,
            error: err,
        } => {
            e.id = pack(connection);
            error(&mut e, err);
            "connection_failed"
        }
        Event::QueryState { query, state } => {
            e.id = pack(query);
            e.state = format!("{state:?}").to_lowercase();
            "query_state"
        }
        Event::Schema {
            query,
            columns,
            lease,
        } => {
            transfer = Some(lease);
            e.id = pack(query);
            e.columns = columns.into_iter().map(column).collect();
            "schema"
        }
        Event::StoredPage {
            query,
            first_row,
            page,
            lease,
        } => {
            let mut stored = self::event(Event::Page { query, page, lease }, leases);
            stored.kind = "stored_page".into();
            stored.first_row = first_row;
            return stored;
        }
        Event::Page { query, page, lease } => {
            transfer = Some(lease);
            e.id = pack(query);
            e.page_index = page.index;
            e.row_count = page.rows.len() as u32;
            e.column_count = page
                .rows
                .first()
                .map(|r| r.len() as u32)
                .unwrap_or_default();
            e.has_more = page.has_more;
            e.cells = page.rows.into_iter().flatten().map(cell).collect();
            "page"
        }
        Event::QueryFinished {
            query,
            duration,
            summary,
        } => {
            e.id = pack(query);
            e.duration_ms = duration.as_millis().min(u64::MAX as u128) as u64;
            e.has_transaction_state = summary.transaction_active.is_some();
            e.transaction_active = summary.transaction_active.unwrap_or_default();
            e.has_affected_rows = summary.affected_rows.is_some();
            e.affected_rows = summary.affected_rows.unwrap_or_default();
            e.warnings = summary.warnings;
            "query_finished"
        }
        Event::QueryFailed { query, error: err } => {
            e.id = pack(query);
            error(&mut e, err);
            "query_failed"
        }
        Event::Metadata {
            request_token,
            connection,
            parent,
            objects,
        } => {
            e.id = pack(connection);
            e.request_token = request_token;
            e.parent = parent.map(|p| p.0).unwrap_or_default();
            e.objects = objects
                .into_iter()
                .map(|o| ffi::MetadataDto {
                    id: o.id.0,
                    parent: o.parent.map(|p| p.0).unwrap_or_default(),
                    name: o.name,
                    qualified_name: o.qualified_name,
                    kind: format!("{:?}", o.kind).to_lowercase(),
                    has_children: o.has_children,
                    has_column: o.column.is_some(),
                    column: o.column.map(column).unwrap_or_default(),
                })
                .collect();
            "metadata"
        }
        Event::TransactionFinished {
            connection,
            committed,
        } => {
            e.id = pack(connection);
            e.committed = committed;
            "transaction_finished"
        }
        Event::MetadataFailed {
            connection,
            parent,
            request_token,
            error: err,
        } => {
            e.id = pack(connection);
            e.parent = parent.map(|p| p.0).unwrap_or_default();
            e.request_token = request_token;
            error(&mut e, err);
            "metadata_failed"
        }
        Event::OperationFailed {
            connection,
            error: err,
        } => {
            e.id = pack(connection);
            error(&mut e, err);
            "operation_failed"
        }
    }
    .into();
    if let Some(lease) = transfer {
        e.reserved_bytes = lease.reserved_bytes() as u64;
        e.lease_id = pack(leases.insert(lease));
        e.has_lease = true;
    }
    e
}

pub(crate) fn profile(value: choscordb_core::ConnectionProfile) -> ffi::ProfileDto {
    let mut dto = ffi::ProfileDto {
        id: value.id,
        name: value.name,
        group_id: value.group_id.unwrap_or_default(),
        credential_ref: value.credential_ref.unwrap_or_default(),
        ..Default::default()
    };
    match value.configuration {
        choscordb_core::ProfileConfiguration::Sqlite { path, read_only } => {
            dto.driver = "sqlite".into();
            dto.path = path;
            dto.read_only = read_only;
        }
        choscordb_core::ProfileConfiguration::Postgres {
            host,
            port,
            database,
            user,
            tls,
        } => {
            dto.driver = "postgres".into();
            dto.host = host;
            dto.port = port;
            dto.database = database;
            dto.user = user;
            dto.tls = match tls.mode {
                TlsMode::Disable => "disable",
                TlsMode::VerifyFull => "verify_full",
            }
            .into();
            dto.root_certificate = tls.root_certificate_path.unwrap_or_default();
        }
    }
    dto
}
