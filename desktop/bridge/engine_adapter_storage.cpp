#include "bridge/engine_adapter_p.h"
#include <utility>

namespace choscordb {
using engine_adapter_detail::toRust;
namespace {
AppearanceLayoutDto appearanceDto(const AppearanceLayout& value) {
    AppearanceLayoutDto dto;
    dto.version = value.version;
    dto.theme = toRust(value.theme);
    dto.density = toRust(value.density);
    dto.accent_kind = toRust(value.accentKind);
    dto.accent = toRust(value.accent);
    dto.navigator_width = value.navigatorWidth;
    dto.editor_results_split = value.editorResultsSplit;
    dto.history_height = value.historyHeight;
    dto.navigator_visible = value.navigatorVisible;
    dto.history_visible = value.historyVisible;
    dto.x = value.x;
    dto.y = value.y;
    dto.width = value.width;
    dto.height = value.height;
    dto.maximized = value.maximized;
    dto.has_screen_name = value.hasScreenName;
    dto.screen_name = toRust(value.screenName);
    return dto;
}
} // namespace
QueryPreferences::QueryPreferences() {
    const auto limits = query_preference_limits();
    version = limits.version;
    pageSize = limits.default_page_size;
    timeoutSeconds = 0;
    connectionTimeoutSeconds = limits.default_connection_timeout_seconds;
}
HistoryPolicy::HistoryPolicy() {
    const auto defaults = history_policy_default();
    enabled = defaults.enabled;
    maxAgeDays = defaults.max_age_days;
    maxRecords = defaults.max_records;
}
EditorPreferences::EditorPreferences() {
    const auto limits = editor_preference_limits();
    version = limits.version;
    fontSize = limits.default_font_size;
}
QueryPreferenceLimits EngineAdapter::queryPreferenceLimits() {
    const auto limits = query_preference_limits();
    return {limits.version,
            limits.min_page_size,
            limits.max_page_size,
            limits.default_page_size,
            limits.max_timeout_seconds,
            limits.default_connection_timeout_seconds,
            limits.max_connection_timeout_seconds};
}
bool EngineAdapter::listHistory(quint32 limit, quint32 offset, quint64 token) {
    return submitRecovery(token, [this, limit, offset, token] {
        return history_list(*d_->engine, limit, offset, token);
    });
}
bool EngineAdapter::searchHistory(const QString& query, quint32 limit, quint64 token,
                                  quint64 offset) {
    const auto utf8 = query.toUtf8();
    return submitRecovery(token, [this, &utf8, limit, token, offset] {
        return history_search(*d_->engine, engine_adapter_detail::utf8View(utf8), limit, offset,
                              token);
    });
}
bool EngineAdapter::clearHistory(quint64 token) {
    d_->pendingHistoryClears.insert(token);
    emit historyClearStarted(token);
    return submitRecovery(token, [this, token] { return history_clear(*d_->engine, token); });
}
bool EngineAdapter::historyClearInProgress() const {
    return !d_->pendingHistoryClears.isEmpty();
}
void EngineAdapter::trackHistoryClears() {
    connect(this, &EngineAdapter::historyCleared, this,
            [this](quint64 token) { d_->pendingHistoryClears.remove(token); });
    connect(this, &EngineAdapter::recoveryFailed, this,
            [this](quint64 token, const QString&) { d_->pendingHistoryClears.remove(token); });
}
bool EngineAdapter::getHistoryPolicy(quint64 token) {
    return submitRecovery(token, [this, token] { return history_policy_get(*d_->engine, token); });
}
bool EngineAdapter::setHistoryPolicy(const HistoryPolicy& policy, quint64 token) {
    return submitRecovery(token, [this, policy, token] {
        HistoryPolicyDto dto;
        dto.enabled = policy.enabled;
        dto.max_age_days = policy.maxAgeDays;
        dto.max_records = policy.maxRecords;
        return history_policy_set(*d_->engine, dto, token);
    });
}
EditorPreferenceLimits EngineAdapter::editorPreferenceLimits() {
    const auto limits = editor_preference_limits();
    return {limits.version, limits.default_font_size, limits.min_font_size, limits.max_font_size};
}
bool EngineAdapter::getQueryPreferences(quint64 token) {
    return submitRecovery(token,
                          [this, token] { return query_preferences_get(*d_->engine, token); });
}
bool EngineAdapter::setQueryPreferences(const QueryPreferences& preferences, quint64 token) {
    return submitRecovery(token, [this, preferences, token] {
        QueryPreferencesDto dto;
        dto.version = preferences.version;
        dto.page_size = preferences.pageSize;
        dto.timeout_seconds = preferences.timeoutSeconds;
        dto.connection_timeout_seconds = preferences.connectionTimeoutSeconds;
        dto.show_system_schemas = preferences.showSystemSchemas;
        return query_preferences_set(*d_->engine, dto, token);
    });
}
bool EngineAdapter::getEditorPreferences(quint64 token) {
    return submitRecovery(token,
                          [this, token] { return editor_preferences_get(*d_->engine, token); });
}
bool EngineAdapter::setEditorPreferences(const EditorPreferences& preferences, quint64 token) {
    return submitRecovery(token, [this, &preferences, token] {
        EditorPreferencesDto dto;
        dto.version = preferences.version;
        dto.font_family = toRust(preferences.fontFamily);
        dto.font_size = preferences.fontSize;
        for (const auto& shortcut : preferences.shortcuts) {
            ShortcutOverrideDto value;
            value.command = toRust(shortcut.command);
            value.sequence = toRust(shortcut.sequence);
            dto.shortcuts.push_back(std::move(value));
        }
        return editor_preferences_set(*d_->engine, std::move(dto), token);
    });
}
bool EngineAdapter::getAppearanceLayout(quint64 token) {
    return submitRecovery(token,
                          [this, token] { return appearance_layout_get(*d_->engine, token); });
}
bool EngineAdapter::setAppearanceLayout(const AppearanceLayout& appearance, quint64 token) {
    return submitRecovery(token, [this, &appearance, token] {
        return appearance_layout_set(*d_->engine, appearanceDto(appearance), token);
    });
}
bool EngineAdapter::resetAppearanceLayout(quint64 token) {
    return submitRecovery(token,
                          [this, token] { return appearance_layout_reset(*d_->engine, token); });
}
bool EngineAdapter::restoreWorkspaceTabs(quint64 token) {
    return submitRecovery(token,
                          [this, token] { return workspace_tabs_restore(*d_->engine, token); });
}
bool EngineAdapter::saveWorkspaceTabs(const QList<SavedWorkspaceTab>& tabs, quint32 activeIndex,
                                      quint64 token) {
    return submitRecovery(token, [this, &tabs, activeIndex, token] {
        rust::Vec<WorkspaceTabDto> values;
        values.reserve(static_cast<size_t>(tabs.size()));
        for (const auto& tab : tabs) {
            WorkspaceTabDto value;
            value.is_object = tab.isObject;
            if (tab.isObject) {
                value.profile_id = toRust(tab.profileId);
                value.object_type = toRust(tab.objectType);
                value.object_id = toRust(tab.objectId);
                value.label = toRust(tab.label);
                value.pane = tab.pane;
            } else {
                const auto& d = tab.document;
                value.document.id = toRust(d.id);
                value.document.title = toRust(d.title);
                value.document.sql = toRust(d.sql);
                value.document.has_profile = !d.profileId.isEmpty();
                value.document.profile_id = toRust(d.profileId);
                value.document.has_file = !d.filePath.isEmpty();
                value.document.file_path = toRust(d.filePath);
                value.document.cursor_offset = d.cursorOffset;
                value.document.selection_anchor = d.selectionAnchor;
                value.document.modified = d.modified;
            }
            values.push_back(std::move(value));
        }
        return workspace_tabs_save(*d_->engine, std::move(values), activeIndex, token);
    });
}
QString EngineAdapter::newDocumentId() {
    return bridge_detail::fromRust(recovery_document_id());
}
QString EngineAdapter::objectTabContext(const QString& profileId, quint64 connection) {
    const auto bytes = profileId.toUtf8();
    return bridge_detail::fromRust(object_tab_context(bridge_detail::utf8View(bytes), connection));
}
EngineAdapter::ObjectTabContext EngineAdapter::parseObjectTabContext(const QString& context) {
    const auto bytes = context.toUtf8();
    const auto dto = parse_object_tab_context(bridge_detail::utf8View(bytes));
    return {dto.session, bridge_detail::fromRust(dto.profile_id)};
}
} // namespace choscordb
