#include "bridge/engine_adapter_p.h"
#include <algorithm>
#include <initializer_list>
#include <utility>

namespace choscordb {
using engine_adapter_detail::rustString;
namespace {
AppearanceLayoutDto appearanceDto(const AppearanceLayout& value) {
    AppearanceLayoutDto dto;
    dto.version = value.version;
    dto.theme = rustString(value.theme);
    dto.density = rustString(value.density);
    dto.accent_kind = rustString(value.accentKind);
    dto.accent = rustString(value.accent);
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
    dto.screen_name = rustString(value.screenName);
    return dto;
}
} // namespace
bool EngineAdapter::listHistory(quint32 limit, quint32 offset, quint64 token) {
    return queueRecovery(token, [this, limit, offset, token] {
        return history_list(*d_->engine, limit, offset, token);
    });
}
bool EngineAdapter::clearHistory(quint64 token) {
    return queueRecovery(token, [this, token] { return history_clear(*d_->engine, token); });
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
            dto.font_family = rustString(preferences.fontFamily);
            dto.font_size = preferences.fontSize;
            for (const auto& shortcut : preferences.shortcuts) {
                ShortcutOverrideDto value;
                value.command = rustString(shortcut.command);
                value.sequence = rustString(shortcut.sequence);
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
    const auto member = [](const QString& value, std::initializer_list<QStringView> choices) {
        return std::ranges::any_of(choices,
                                   [&value](QStringView choice) { return value == choice; });
    };
    if (!appearance.theme.isValidUtf16() || !appearance.density.isValidUtf16() ||
        !appearance.accentKind.isValidUtf16() || !appearance.accent.isValidUtf16() ||
        !appearance.screenName.isValidUtf16() || appearance.theme.size() > 16 ||
        appearance.density.size() > 16 || appearance.accentKind.size() > 16 ||
        appearance.accent.size() > 16 || appearance.screenName.size() > 256) {
        emit recoveryFailed(token, tr("Invalid appearance or layout settings."));
        return false;
    }
    const bool preset = appearance.accentKind == "preset";
    const bool validCustom =
        appearance.accent.size() == 7 && appearance.accent.front() == '#' &&
        std::ranges::all_of(appearance.accent.sliced(1), [](QChar value) {
            const auto c = value.unicode();
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        });
    const auto screenBytes = appearance.screenName.toUtf8();
    const quint64 retainedBytes = static_cast<quint64>(appearance.theme.toUtf8().size()) +
                                  static_cast<quint64>(appearance.density.toUtf8().size()) +
                                  static_cast<quint64>(appearance.accentKind.toUtf8().size()) +
                                  static_cast<quint64>(appearance.accent.toUtf8().size()) +
                                  static_cast<quint64>(screenBytes.size());
    if (appearance.version != 1 || !member(appearance.theme, {u"system", u"light", u"dark"}) ||
        !member(appearance.density, {u"compact", u"comfortable"}) ||
        !member(appearance.accentKind, {u"preset", u"custom"}) ||
        (preset && !member(appearance.accent, {u"cobalt", u"azure", u"violet", u"teal", u"green",
                                               u"orange", u"rose"})) ||
        (!preset && !validCustom) || appearance.navigatorWidth < 96 ||
        appearance.navigatorWidth > 2048 || appearance.editorResultsSplit < 100 ||
        appearance.editorResultsSplit > 900 || appearance.historyHeight < 80 ||
        appearance.historyHeight > 4096 || appearance.width < 960 || appearance.height < 640 ||
        appearance.width > 16384 || appearance.height > 16384 || appearance.x < -1000000 ||
        appearance.x > 1000000 || appearance.y < -1000000 || appearance.y > 1000000 ||
        screenBytes.size() > 256 ||
        (appearance.hasScreenName && (screenBytes.isEmpty() || screenBytes.contains('\0'))) ||
        retainedBytes > 4096) {
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
bool EngineAdapter::restoreWorkspace(quint64 token) {
    return queueRecovery(token, [this, token] { return workspace_restore(*d_->engine, token); });
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
                    value.profile_id = rustString(tab.profileId);
                    value.object_type = rustString(tab.objectType);
                    value.object_id = rustString(tab.objectId);
                    value.label = rustString(tab.label);
                    value.pane = tab.pane;
                } else {
                    const auto& d = tab.document;
                    value.document.id = rustString(d.id);
                    value.document.title = rustString(d.title);
                    const auto sql = d.sql.toUtf8();
                    value.document.sql =
                        rust::String(sql.constData(), static_cast<size_t>(sql.size()));
                    value.document.has_profile = !d.profileId.isEmpty();
                    value.document.profile_id = rustString(d.profileId);
                    value.document.has_file = !d.filePath.isEmpty();
                    value.document.file_path = rustString(d.filePath);
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
bool EngineAdapter::saveWorkspace(const QList<SavedEditorDocument>& documents, quint64 token) {
    // Transport guards precede CXX copies. Core additionally validates escaped
    // serialized size and all fields against the authoritative storage contract.
    const auto limits = recoveryLimits();
    if (static_cast<quint64>(documents.size()) > limits.maxDocuments) {
        emit recoveryFailed(token, tr("Workspace recovery limit reached."));
        return false;
    }
    quint64 totalBytes = 0;
    for (const auto& d : documents) {
        for (const auto* text : {&d.id, &d.title, &d.sql, &d.profileId, &d.filePath}) {
            const auto retained =
                static_cast<quint64>(qMax(text->size(), text->capacity())) * sizeof(QChar);
            if (retained > limits.maxCollectionBytes * 2 - totalBytes) {
                emit recoveryFailed(token, tr("Workspace recovery limit reached."));
                return false;
            }
            totalBytes += retained;
        }
    }
    if (totalBytes > limits.maxCollectionBytes * 2) {
        emit recoveryFailed(token, tr("Workspace recovery limit reached."));
        return false;
    }
    return queueRecovery(
        token,
        [this, documents, token, limits]() {
            quint64 totalBytes = 0;
            rust::Vec<EditorDocumentDto> values;
            values.reserve(static_cast<size_t>(documents.size()));
            for (const auto& d : documents) {
                if (static_cast<quint64>(d.sql.size()) > limits.maxSqlBytes || d.id.size() > 256 ||
                    d.title.size() > 1024 || d.profileId.size() > 256 ||
                    d.filePath.size() > 16 * 1024) {
                    Submit rejected;
                    rejected.error = rustString(tr("Workspace recovery limit reached."));
                    return rejected;
                }
                const auto sql = d.sql.toUtf8();
                totalBytes += static_cast<quint64>(sql.size());
                if (static_cast<quint64>(sql.size()) > limits.maxSqlBytes ||
                    totalBytes > limits.maxCollectionBytes) {
                    Submit rejected;
                    rejected.error = rustString(tr("Workspace recovery limit reached."));
                    return rejected;
                }
                EditorDocumentDto value;
                value.id = rustString(d.id);
                value.title = rustString(d.title);
                value.sql = rust::String(sql.constData(), static_cast<size_t>(sql.size()));
                value.has_profile = !d.profileId.isEmpty();
                value.profile_id = rustString(d.profileId);
                value.has_file = !d.filePath.isEmpty();
                value.file_path = rustString(d.filePath);
                value.cursor_offset = d.cursorOffset;
                value.selection_anchor = d.selectionAnchor;
                value.modified = d.modified;
                values.push_back(std::move(value));
            }
            return workspace_save(*d_->engine, std::move(values), token);
        },
        totalBytes);
}
} // namespace choscordb
