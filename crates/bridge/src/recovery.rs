//! Owned typed recovery transport; persistence and validation remain in core.
use crate::{BridgeEngine, ffi, submit};
pub fn workspace_save(
    engine: &mut BridgeEngine,
    documents: Vec<ffi::EditorDocumentDto>,
    token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        e.workspace_save(
            documents
                .into_iter()
                .map(|d| choscordb_core::EditorDocument {
                    id: d.id,
                    title: d.title,
                    sql: d.sql,
                    profile_id: d.has_profile.then_some(d.profile_id),
                    file_path: d.has_file.then_some(d.file_path),
                    cursor_offset: d.cursor_offset,
                    selection_anchor: d.selection_anchor,
                    modified: d.modified,
                })
                .collect(),
            token,
        )
        .map(|()| token)
        .map_err(|e| e.to_string())
    })
}
pub fn workspace_restore(engine: &mut BridgeEngine, token: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.workspace_restore(token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
pub fn workspace_tabs_save(
    engine: &mut BridgeEngine,
    tabs: Vec<ffi::WorkspaceTabDto>,
    active_tab: u32,
    token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        let tabs = tabs
            .into_iter()
            .map(|tab| {
                if tab.is_object {
                    choscordb_core::WorkspaceTab::Object(choscordb_core::ObjectTab {
                        profile_id: tab.profile_id,
                        object_type: tab.object_type,
                        object_id: tab.object_id,
                        label: tab.label,
                        pane: tab.pane,
                    })
                } else {
                    choscordb_core::WorkspaceTab::Sql(from_document(tab.document))
                }
            })
            .collect();
        e.workspace_tabs_save(
            choscordb_core::WorkspaceSnapshot {
                tabs,
                active_index: active_tab as usize,
            },
            token,
        )
        .map(|()| token)
        .map_err(|e| e.to_string())
    })
}
pub fn workspace_tabs_restore(engine: &mut BridgeEngine, token: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.workspace_tabs_restore(token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
fn from_document(d: ffi::EditorDocumentDto) -> choscordb_core::EditorDocument {
    choscordb_core::EditorDocument {
        id: d.id,
        title: d.title,
        sql: d.sql,
        profile_id: d.has_profile.then_some(d.profile_id),
        file_path: d.has_file.then_some(d.file_path),
        cursor_offset: d.cursor_offset,
        selection_anchor: d.selection_anchor,
        modified: d.modified,
    }
}
pub(crate) fn workspace_tab(tab: choscordb_core::WorkspaceTab) -> ffi::WorkspaceTabDto {
    match tab {
        choscordb_core::WorkspaceTab::Sql(d) => ffi::WorkspaceTabDto {
            document: document(d),
            ..Default::default()
        },
        choscordb_core::WorkspaceTab::Object(o) => ffi::WorkspaceTabDto {
            is_object: true,
            profile_id: o.profile_id,
            object_type: o.object_type,
            object_id: o.object_id,
            label: o.label,
            pane: o.pane,
            ..Default::default()
        },
    }
}
pub fn history_list(engine: &mut BridgeEngine, limit: u32, offset: u32, token: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.history_list(limit, offset, token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
pub fn history_clear(engine: &mut BridgeEngine, token: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.history_clear(token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
pub fn history_policy_get(engine: &mut BridgeEngine, token: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.history_policy_get(token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
pub fn history_policy_set(
    engine: &mut BridgeEngine,
    policy: ffi::HistoryPolicyDto,
    token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        e.history_policy_set(
            choscordb_core::HistoryPolicy {
                enabled: policy.enabled,
                max_age_days: policy.max_age_days,
                max_records: policy.max_records,
            },
            token,
        )
        .map(|()| token)
        .map_err(|e| e.to_string())
    })
}
pub(crate) fn document(d: choscordb_core::EditorDocument) -> ffi::EditorDocumentDto {
    ffi::EditorDocumentDto {
        id: d.id,
        title: d.title,
        sql: d.sql,
        has_profile: d.profile_id.is_some(),
        profile_id: d.profile_id.unwrap_or_default(),
        has_file: d.file_path.is_some(),
        file_path: d.file_path.unwrap_or_default(),
        cursor_offset: d.cursor_offset,
        selection_anchor: d.selection_anchor,
        modified: d.modified,
    }
}
pub(crate) fn history_entry(h: choscordb_core::HistoryEntry) -> ffi::HistoryEntryDto {
    use choscordb_core::HistoryStatus;
    ffi::HistoryEntryDto {
        id: h.id,
        has_profile: h.profile_id.is_some(),
        profile_id: h.profile_id.unwrap_or_default(),
        sql: h.sql,
        timestamp: h.timestamp,
        duration_ms: h.duration_ms,
        status: match h.status {
            HistoryStatus::Completed => "completed",
            HistoryStatus::Failed => "failed",
            HistoryStatus::Cancelled => "cancelled",
            HistoryStatus::Disconnected => "disconnected",
        }
        .into(),
        has_row_count: h.row_count.is_some(),
        row_count: h.row_count.unwrap_or_default(),
    }
}

pub fn recovery_limits() -> ffi::RecoveryLimitsDto {
    ffi::RecoveryLimitsDto {
        max_documents: choscordb_core::MAX_WORKSPACE_DOCUMENTS as u64,
        max_sql_bytes: choscordb_core::MAX_SQL_BYTES as u64,
        max_collection_bytes: choscordb_core::MAX_COLLECTION_BYTES as u64,
    }
}

pub fn history_flush(engine: &mut BridgeEngine, token: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.history_flush(token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
