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
    return queueRecovery(token, [this, limit, offset, token] {
        return history_list(*d_->engine, limit, offset, token);
    });
}
bool EngineAdapter::searchHistory(const QString& query, quint32 limit, quint64 token,
                                  quint64 offset) {
    const auto utf8 = query.toUtf8();
    if (!query.isValidUtf16() || utf8.size() > 1024 || limit == 0 || limit > 100) {
        emit recoveryFailed(token, tr("History search limit exceeded."));
        return false;
    }
    return queueRecovery(
        token,
        [this, utf8, limit, token, offset] {
            return history_search(*d_->engine, engine_adapter_detail::utf8View(utf8), limit, offset,
                                  token);
        },
        quint64(utf8.size()));
}
bool EngineAdapter::clearHistory(quint64 token) {
    d_->pendingHistoryClears.insert(token);
    emit historyClearStarted(token);
    return queueRecovery(token, [this, token] { return history_clear(*d_->engine, token); });
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
    return queueRecovery(token, [this, token] { return history_policy_get(*d_->engine, token); });
}
bool EngineAdapter::setHistoryPolicy(const HistoryPolicy& policy, quint64 token) {
    return queueRecovery(token, [this, policy, token] {
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
    return queueRecovery(token,
                         [this, token] { return query_preferences_get(*d_->engine, token); });
}
bool EngineAdapter::setQueryPreferences(const QueryPreferences& preferences, quint64 token) {
    return queueRecovery(token, [this, preferences, token] {
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
    return queueRecovery(token,
                         [this, token] { return editor_preferences_get(*d_->engine, token); });
}
bool EngineAdapter::setEditorPreferences(const EditorPreferences& preferences, quint64 token) {
    if (preferences.shortcuts.size() > 16 || preferences.fontFamily.size() > 256 ||
        !preferences.fontFamily.isValidUtf16()) {
        emit recoveryFailed(token, tr("Invalid editor preferences"));
        return false;
    }
    for (const auto& shortcut : preferences.shortcuts) {
        if (shortcut.command.size() > 128 || shortcut.sequence.size() > 128 ||
            !shortcut.command.isValidUtf16() || !shortcut.sequence.isValidUtf16()) {
            emit recoveryFailed(token, tr("Invalid keyboard shortcut"));
            return false;
        }
    }
    auto bounded = preferences;
    bounded.fontFamily.squeeze();
    for (auto& shortcut : bounded.shortcuts) {
        shortcut.command.squeeze();
        shortcut.sequence.squeeze();
    }
    bounded.shortcuts.squeeze();
    return queueRecovery(
        token,
        [this, preferences = std::move(bounded), token] {
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
        },
        9216);
}
bool EngineAdapter::getAppearanceLayout(quint64 token) {
    return queueRecovery(token,
                         [this, token] { return appearance_layout_get(*d_->engine, token); });
}
bool EngineAdapter::setAppearanceLayout(const AppearanceLayout& appearance, quint64 token) {
    if (!appearance.theme.isValidUtf16() || !appearance.density.isValidUtf16() ||
        !appearance.accentKind.isValidUtf16() || !appearance.accent.isValidUtf16() ||
        !appearance.screenName.isValidUtf16()) {
        emit recoveryFailed(token, tr("Invalid appearance or layout settings."));
        return false;
    }
    const auto screenBytes = appearance.screenName.toUtf8();
    const quint64 retainedBytes = static_cast<quint64>(appearance.theme.toUtf8().size()) +
                                  static_cast<quint64>(appearance.density.toUtf8().size()) +
                                  static_cast<quint64>(appearance.accentKind.toUtf8().size()) +
                                  static_cast<quint64>(appearance.accent.toUtf8().size()) +
                                  static_cast<quint64>(screenBytes.size());
    if (screenBytes.size() > 256 || retainedBytes > 4096) {
        emit recoveryFailed(token, tr("Invalid appearance or layout settings."));
        return false;
    }
    return queueRecovery(
        token,
        [this, appearance, token] {
            return appearance_layout_set(*d_->engine, appearanceDto(appearance), token);
        },
        static_cast<quint64>(retainedBytes));
}
bool EngineAdapter::resetAppearanceLayout(quint64 token) {
    return queueRecovery(token,
                         [this, token] { return appearance_layout_reset(*d_->engine, token); });
}
bool EngineAdapter::restoreWorkspaceTabs(quint64 token) {
    return queueRecovery(token,
                         [this, token] { return workspace_tabs_restore(*d_->engine, token); });
}
bool EngineAdapter::saveWorkspaceTabs(const QList<SavedWorkspaceTab>& tabs, quint32 activeIndex,
                                      quint64 token) {
    const auto limits = recoveryLimits();
    if (static_cast<quint64>(tabs.size()) > limits.maxDocuments ||
        (tabs.isEmpty() ? activeIndex != 0 : activeIndex >= static_cast<quint32>(tabs.size()))) {
        emit recoveryFailed(token, tr("Workspace recovery limit reached."));
        return false;
    }
    quint64 retained = 0;
    for (const auto& tab : tabs) {
        for (const auto* field :
             {&tab.document.id, &tab.document.title, &tab.document.sql, &tab.document.profileId,
              &tab.document.filePath, &tab.profileId, &tab.objectType, &tab.objectId, &tab.label}) {
            retained +=
                static_cast<quint64>(qMax(field->size(), field->capacity())) * sizeof(QChar);
            if (retained > limits.maxCollectionBytes * 2) {
                emit recoveryFailed(token, tr("Workspace recovery limit reached."));
                return false;
            }
        }
    }
    return queueRecovery(
        token,
        [this, tabs, activeIndex, token] {
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
        },
        retained);
}
} // namespace choscordb
