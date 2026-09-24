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
fn edit_target(target: EditTarget) -> ffi::EditTargetDto {
    ffi::EditTargetDto {
        qualified_name: target.qualified_name,
        parameter_style: target.parameter_style,
        columns: target
            .columns
            .into_iter()
            .map(|column| ffi::EditColumnDto {
                name: column.name,
                database_type: column.database_type,
                nullable: column.nullable,
                generated: column.generated,
                key: column.key,
            })
            .collect(),
        key_columns: target.key_columns,
        reason: target.reason,
    }
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
        Event::SshHostKeysInspected {
            request_token,
            candidates,
        } => {
            e.request_token = request_token;
            e.host_key_candidates = candidates
                .into_iter()
                .map(|candidate| {
                    let (target_kind, target_id, target_index) = match &candidate.target {
                        choscordb_driver_api::SshHostKeyTarget::Target => {
                            ("target", String::new(), 0)
                        }
                        choscordb_driver_api::SshHostKeyTarget::Jump(id) => ("jump", id.clone(), 0),
                        choscordb_driver_api::SshHostKeyTarget::JumpIndex(index) => {
                            ("jump_index", String::new(), *index as u32)
                        }
                    };
                    ffi::SshHostKeyCandidateDto {
                        target_kind: target_kind.into(),
                        target_id,
                        target_index,
                        original_host: candidate.original_host.clone(),
                        hostname: candidate.hostname.clone(),
                        port: candidate.port,
                        host_key_alias: candidate.host_key_alias.clone().unwrap_or_default(),
                        key_type: candidate.key_type.clone(),
                        public_key: candidate.public_key.clone(),
                        sha256: candidate.sha256.clone(),
                        opaque_json: serde_json::to_string(&candidate)
                            .expect("validated SSH host key candidate serializes"),
                    }
                })
                .collect();
            "ssh_host_keys_inspected"
        }
        Event::SshHostKeyApproved {
            request_token,
            outcome,
        } => {
            e.request_token = request_token;
            e.host_key_approval = match outcome {
                choscordb_driver_api::SshHostKeyApproval::Approved => "approved",
                choscordb_driver_api::SshHostKeyApproval::OutcomeUnknown => "outcome_unknown",
            }
            .into();
            "ssh_host_key_approved"
        }
        Event::SshHostKeyFailed {
            request_token,
            error: err,
        } => {
            e.request_token = request_token;
            error(&mut e, err);
            "ssh_host_key_failed"
        }
        Event::EditQuery {
            connection,
            request_token,
            query,
        } => {
            e.id = pack(connection);
            e.request_token = request_token;
            e.edit_target = edit_target(query.target);
            e.edit_source_columns = query.source_columns;
            e.edit_target.reason = query.reason;
            "edit_query"
        }
        Event::EditQueryFailed {
            connection,
            request_token,
            error: err,
        } => {
            e.id = pack(connection);
            e.request_token = request_token;
            error(&mut e, err);
            "edit_query_failed"
        }
        Event::EditTarget {
            connection,
            request_token,
            target,
        } => {
            e.id = pack(connection);
            e.request_token = request_token;
            e.edit_target = edit_target(target);
            "edit_target"
        }
        Event::EditTargetFailed {
            connection,
            request_token,
            error: err,
        } => {
            e.id = pack(connection);
            e.request_token = request_token;
            error(&mut e, err);
            "edit_target_failed"
        }
        Event::EditApplied {
            connection,
            request_token,
            summary,
        } => {
            e.id = pack(connection);
            e.request_token = request_token;
            e.edit_affected_rows = summary.affected_rows;
            "edit_applied"
        }
        Event::EditFailed {
            connection,
            request_token,
            error: err,
        } => {
            e.id = pack(connection);
            e.request_token = request_token;
            error(&mut e, err);
            "edit_failed"
        }
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
        Event::WorkspaceTabsRestored {
            request_token,
            snapshot,
        } => {
            e.request_token = request_token;
            e.active_tab = snapshot.active_index as u32;
            e.workspace_tabs = snapshot
                .tabs
                .into_iter()
                .map(super::recovery::workspace_tab)
                .collect();
            "workspace_tabs_restored"
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
            e.profiles = vec![profile(*value)];
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
            request_token,
        } => {
            e.id = pack(connection);
            e.object = object.0;
            e.ddl = ddl;
            e.request_token = request_token;
            "ddl"
        }
        Event::DdlFailed {
            connection,
            object,
            request_token,
            error: err,
        } => {
            e.id = pack(connection);
            e.object = object.0;
            e.request_token = request_token;
            error(&mut e, err);
            "ddl_failed"
        }
        Event::SessionSqlMode {
            connection,
            mode,
            request_token,
        } => {
            e.request_token = request_token;
            e.id = pack(connection);
            e.sql_mode = mode;
            e.has_sql_mode = true;
            "session_sql_mode"
        }
        Event::Connected {
            transaction_active,
            connection,
            capabilities: c,
        } => {
            e.id = pack(connection);
            e.capabilities = capabilities(c);
            e.has_transaction_state = transaction_active.is_some();
            e.transaction_active = transaction_active.unwrap_or(false);
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
        Event::ResultViewProgress {
            query,
            scanned_rows,
            buffered_rows,
        } => {
            e.id = pack(query);
            e.result_view_scanned_rows = scanned_rows;
            e.result_view_buffered_rows = buffered_rows;
            "result_view_progress"
        }
        Event::ResultViewApplied { query, rows } => {
            e.id = pack(query);
            e.result_view_rows = rows;
            "result_view_applied"
        }
        Event::ResultViewFailed { query, error: err } => {
            e.id = pack(query);
            error(&mut e, err);
            "result_view_failed"
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
            e.has_more_results = summary.has_more_results;
            e.has_sql_mode = summary.sql_mode.is_some();
            e.sql_mode = summary.sql_mode.unwrap_or_default();
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
            offset,
            next_offset,
        } => {
            e.metadata_offset = offset;
            e.has_more_metadata = next_offset.is_some();
            e.next_metadata_offset = next_offset.unwrap_or_default();
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
                    properties: o
                        .properties
                        .into_iter()
                        .map(|p| ffi::MetadataPropertyDto {
                            name: p.name,
                            value: p.value,
                            availability: format!("{:?}", p.availability).to_lowercase(),
                            reason: p.reason,
                        })
                        .collect(),
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
        authentication: serde_json::to_string(&value.authentication)
            .expect("authentication settings serialize"),
        id: value.id,
        name: value.name,
        group_id: value.group_id.unwrap_or_default(),
        credential_ref: value.credential_ref.unwrap_or_default(),
        ssh_credential_ref: value.ssh_credential_ref.unwrap_or_default(),
        ssh_private_key_ref: value.ssh_private_key_ref.unwrap_or_default(),
        tls_credential_ref: value.tls_credential_ref.unwrap_or_default(),
        proxy_credential_ref: value.proxy_credential_ref.unwrap_or_default(),
        ssh_jump_credential_refs: serde_json::to_string(&value.ssh_jump_credential_refs)
            .unwrap_or_default(),
        ssh_jump_private_key_refs: serde_json::to_string(&value.ssh_jump_private_key_refs)
            .unwrap_or_default(),
        ..Default::default()
    };
    match value.configuration {
        choscordb_core::ProfileConfiguration::Sqlite { path, read_only } => {
            dto.driver = "sqlite".into();
            dto.path = path;
            dto.read_only = read_only;
        }
        choscordb_core::ProfileConfiguration::Mysql {
            proxy,
            host,
            port,
            database,
            user,
            tls,
            ssh,
        } => {
            dto.driver = "mysql".into();
            dto.proxy_options = proxy
                .map(|proxy| serde_json::to_string(&proxy).expect("proxy settings serialize"))
                .unwrap_or_default();
            dto.host = host;
            dto.port = port;
            dto.database = database;
            dto.user = user;
            dto.tls = match tls.mode {
                TlsMode::Disable => "disable",
                TlsMode::VerifyFull => "verify_full",
                TlsMode::VerifyCa => "verify_ca",
                TlsMode::Require => "require",
                TlsMode::Prefer => "prefer",
            }
            .into();
            dto.root_certificate = tls.root_certificate_path.unwrap_or_default();
            dto.tls_client_identity = tls.client_identity_path.unwrap_or_default();
            if let Some(ssh) = ssh {
                dto.ssh_enabled = true;
                dto.ssh_options =
                    serde_json::to_string(&ssh.options).expect("SSH options serialize");
                dto.ssh_host = ssh.host;
                dto.ssh_port = ssh.port;
                dto.ssh_user = ssh.user;
                dto.ssh_authentication = match ssh.authentication {
                    SshAuthentication::Agent => "agent",
                    SshAuthentication::PublicKey => "public_key",
                    SshAuthentication::Password => "password",
                }
                .into();
                dto.ssh_identity_source = match ssh.identity_source {
                    choscordb_driver_api::SshIdentitySource::File => "file",
                    choscordb_driver_api::SshIdentitySource::Inline => "inline",
                }
                .into();
                dto.ssh_identity_file = ssh.identity_file.unwrap_or_default();
            }
        }
        choscordb_core::ProfileConfiguration::Postgres {
            proxy,
            host,
            port,
            database,
            user,
            tls,
            ssh,
        } => {
            dto.driver = "postgres".into();
            dto.proxy_options = proxy
                .map(|proxy| serde_json::to_string(&proxy).expect("proxy settings serialize"))
                .unwrap_or_default();
            dto.host = host;
            dto.port = port;
            dto.database = database;
            dto.user = user;
            dto.tls = match tls.mode {
                TlsMode::Disable => "disable",
                TlsMode::VerifyFull => "verify_full",
                TlsMode::VerifyCa => "verify_ca",
                TlsMode::Require => "require",
                TlsMode::Prefer => "prefer",
            }
            .into();
            dto.root_certificate = tls.root_certificate_path.unwrap_or_default();
            dto.tls_client_identity = tls.client_identity_path.unwrap_or_default();
            if let Some(ssh) = ssh {
                dto.ssh_enabled = true;
                dto.ssh_options =
                    serde_json::to_string(&ssh.options).expect("SSH options serialize");
                dto.ssh_host = ssh.host;
                dto.ssh_port = ssh.port;
                dto.ssh_user = ssh.user;
                dto.ssh_authentication = match ssh.authentication {
                    SshAuthentication::Agent => "agent",
                    SshAuthentication::PublicKey => "public_key",
                    SshAuthentication::Password => "password",
                }
                .into();
                dto.ssh_identity_source = match ssh.identity_source {
                    choscordb_driver_api::SshIdentitySource::File => "file",
                    choscordb_driver_api::SshIdentitySource::Inline => "inline",
                }
                .into();
                dto.ssh_identity_file = ssh.identity_file.unwrap_or_default();
            }
        }
    }
    dto
}
