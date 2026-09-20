#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <vector>
namespace choscordb {
namespace {
rust::Str utf8View(const QByteArray& bytes) {
    return rust::Str(bytes.constData(), static_cast<size_t>(bytes.size()));
}
QString string(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
rust::String rustString(const QString& value) {
    const auto bytes = value.toUtf8();
    return rust::String(bytes.constData(), static_cast<size_t>(bytes.size()));
}
ProfileDto profileDto(const SavedProfile& value) {
    ProfileDto dto;
    dto.group_id = rustString(value.groupId);
    dto.id = rustString(value.id);
    dto.name = rustString(value.name);
    dto.driver = rustString(value.driver);
    dto.path = rustString(value.path);
    dto.read_only = value.readOnly;
    dto.host = rustString(value.host);
    dto.port = value.port;
    dto.database = rustString(value.database);
    dto.user = rustString(value.user);
    dto.tls = rustString(value.tls);
    dto.root_certificate = rustString(value.rootCertificate);
    dto.credential_ref = rustString(value.credentialRef);
    dto.ssh_enabled = value.sshEnabled;
    dto.ssh_host = rustString(value.sshHost);
    dto.ssh_port = value.sshPort;
    dto.ssh_user = rustString(value.sshUser);
    dto.ssh_identity_file = rustString(value.sshIdentityFile);
    return dto;
}
SavedProfile savedProfile(const ProfileDto& dto) {
    SavedProfile value;
    value.groupId = string(dto.group_id);
    value.id = string(dto.id);
    value.name = string(dto.name);
    value.driver = string(dto.driver);
    value.path = string(dto.path);
    value.readOnly = dto.read_only;
    value.host = string(dto.host);
    value.port = dto.port;
    value.database = string(dto.database);
    value.user = string(dto.user);
    value.tls = dto.tls.empty() ? QStringLiteral("verify_full") : string(dto.tls);
    value.rootCertificate = string(dto.root_certificate);
    value.credentialRef = string(dto.credential_ref);
    value.sshEnabled = dto.ssh_enabled;
    value.sshHost = string(dto.ssh_host);
    value.sshPort = dto.ssh_port ? dto.ssh_port : 22;
    value.sshUser = string(dto.ssh_user);
    value.sshIdentityFile = string(dto.ssh_identity_file);
    return value;
}
AppearanceLayout appearanceLayout(const AppearanceLayoutDto& dto) {
    return {dto.version,
            string(dto.theme),
            string(dto.density),
            string(dto.accent_kind),
            string(dto.accent),
            dto.navigator_width,
            dto.history_height,
            dto.editor_results_split,
            dto.navigator_visible,
            dto.history_visible,
            dto.x,
            dto.y,
            dto.width,
            dto.height,
            dto.maximized,
            dto.has_screen_name,
            string(dto.screen_name)};
}
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
QueryPreferences::QueryPreferences() {
    const auto limits = query_preference_limits();
    version = limits.version;
    pageSize = limits.default_page_size;
    timeoutSeconds = 0;
}
QueryPreferenceLimits EngineAdapter::queryPreferenceLimits() {
    const auto limits = query_preference_limits();
    return {limits.version, limits.min_page_size, limits.max_page_size, limits.default_page_size,
            limits.max_timeout_seconds};
}
struct EngineAdapter::Private {
    explicit Private(const QString& path)
        : engine(path.isEmpty() ? new_engine() : new_engine_with_storage(utf8View(path.toUtf8()))) {
    }
    rust::Box<BridgeEngine> engine;
    struct RecoveryRequest {
        quint64 token;
        std::function<Submit()> command;
        quint64 bytes;
    };
    QQueue<RecoveryRequest> recoveryQueue;
    std::optional<quint64> activeRecovery;
    quint64 queuedRecoveryBytes = 0;
    QSet<quint64> connections;
    struct QueryPaging {
        quint64 connection;
        quint32 pageSize;
    };
    QHash<quint64, QueryPaging> queryPaging;
    struct InspectionRequest {
        quint64 connection, token;
        QString object;
        ObjectInspectionPane pane;
    };
    QHash<quint64, InspectionRequest> inspections;
    quint64 nextInspectionToken = quint64(1) << 63;
    bool closing = false, stopping = false;
    std::optional<quint64> shutdownToken;
    QString shutdownHistoryError;
    struct Transfer {
        quint64 reserved;
        std::optional<quint64> retained;
        bool released = false;
    };
    QHash<quint64, Transfer> transfers;
};
EngineAdapter::EngineAdapter(QObject* parent, const QString& storagePath)
    : QObject(parent), d_(std::make_unique<Private>(storagePath)) {
    connect(this, &EngineAdapter::eventReady, this, [this](const BridgeEvent& event) {
        const auto kind = string(event.kind);
        if (kind == "metadata" || kind == "metadata_failed" || kind == "ddl" ||
            kind == "ddl_failed") {
            const auto it = d_->inspections.find(event.request_token);
            if (it != d_->inspections.end() && it->connection == event.id &&
                it->object ==
                    (kind.startsWith("ddl") ? string(event.object) : string(event.parent))) {
                const auto request = it.value();
                d_->inspections.erase(it);
                ObjectInspection result;
                result.pane = request.pane;
                if (kind.endsWith("_failed")) {
                    if (string(event.error_kind) == "Unsupported") {
                        result.availability = MetadataAvailability::Unsupported;
                        result.reason = string(event.error);
                        emit objectInspectionReady(request.connection, request.object,
                                                   request.token, result);
                    } else {
                        emit objectInspectionFailed(request.connection, request.object,
                                                    request.token, string(event.error));
                    }
                } else {
                    result.ddl = string(event.ddl);
                    for (const auto& object : event.objects) {
                        const auto objectKind = string(object.kind);
                        const bool include =
                            (request.pane == ObjectInspectionPane::Columns &&
                             objectKind == "column") ||
                            (request.pane == ObjectInspectionPane::Indexes &&
                             objectKind == "index") ||
                            (request.pane == ObjectInspectionPane::Keys &&
                             (objectKind == "primarykey" || objectKind == "foreignkey" ||
                              objectKind == "uniquekey"));
                        if (!include)
                            continue;
                        ObjectInspectionRow row;
                        row.id = string(object.id);
                        row.name = string(object.name);
                        row.kind = objectKind;
                        if (object.has_column) {
                            row.properties.append({tr("Type"),
                                                   string(object.column.database_type),
                                                   MetadataAvailability::Available,
                                                   {}});
                            row.properties.append(
                                {tr("Nullable"),
                                 object.column.nullability < 0
                                     ? QString()
                                     : (object.column.nullability == 1 ? tr("Yes") : tr("No")),
                                 object.column.nullability < 0 ? MetadataAvailability::Unavailable
                                                               : MetadataAvailability::Available,
                                 object.column.nullability < 0
                                     ? tr("The driver cannot determine nullability")
                                     : QString()});
                        }
                        for (const auto& property : object.properties) {
                            const auto availability = string(property.availability);
                            row.properties.append(
                                {string(property.name), string(property.value),
                                 availability == "unsupported"   ? MetadataAvailability::Unsupported
                                 : availability == "unavailable" ? MetadataAvailability::Unavailable
                                                                 : MetadataAvailability::Available,
                                 string(property.reason)});
                        }
                        result.rows.append(row);
                    }
                    emit objectInspectionReady(request.connection, request.object, request.token,
                                               result);
                }
            }
        }
        if (kind == "disconnected" || kind == "connection_failed") {
            d_->connections.remove(event.id);
            for (auto it = d_->inspections.begin(); it != d_->inspections.end();) {
                if (it->connection == event.id) {
                    const auto request = it.value();
                    it = d_->inspections.erase(it);
                    emit objectInspectionFailed(request.connection, request.object, request.token,
                                                tr("Connection disconnected"));
                } else
                    ++it;
            }
            for (auto it = d_->queryPaging.begin(); it != d_->queryPaging.end();) {
                if (it->connection == event.id)
                    it = d_->queryPaging.erase(it);
                else
                    ++it;
            }
            if (d_->closing)
                QTimer::singleShot(0, this, &EngineAdapter::finishShutdown);
        }
        if (kind == "history_write_failed") {
            if (d_->closing)
                d_->shutdownHistoryError = string(event.error);
            emit historyWriteFailed(event.id, string(event.error));
        }
        if (kind == "history_flushed" && d_->shutdownToken == event.request_token) {
            d_->shutdownToken.reset();
            if (!d_->shutdownHistoryError.isEmpty()) {
                d_->closing = false;
                emit shutdownFailed(
                    tr("Some query history could not be saved: %1").arg(d_->shutdownHistoryError),
                    false);
            } else {
                shutdown();
                emit shutdownReady();
            }
        }
        const bool recoveryTerminal =
            kind == "workspace_restored" || kind == "workspace_tabs_restored" ||
            kind == "workspace_saved" || kind == "recovery_failed" || kind == "history_listed" ||
            kind == "history_cleared" || kind == "history_policy" || kind == "history_flushed" ||
            kind == "editor_preferences" || kind == "query_preferences" ||
            kind == "appearance_layout";
        if (recoveryTerminal && d_->activeRecovery == event.request_token) {
            const auto token = event.request_token;
            QTimer::singleShot(0, this, [this, token] {
                if (d_->activeRecovery == token) {
                    d_->activeRecovery.reset();
                    pumpRecovery();
                }
            });
        }
        if (kind == "query_preferences") {
            QueryPreferences preferences;
            preferences.version = event.query_preferences.version;
            preferences.pageSize = event.query_preferences.page_size;
            preferences.timeoutSeconds = event.query_preferences.timeout_seconds;
            emit queryPreferencesReady(event.request_token, preferences);
        } else if (kind == "appearance_layout") {
            emit appearanceLayoutReady(event.request_token, event.has_appearance,
                                       appearanceLayout(event.appearance_layout));
        } else if (kind == "editor_preferences") {
            const auto& value = event.editor_preferences;
            EditorPreferences preferences;
            preferences.version = value.version;
            preferences.fontFamily = string(value.font_family);
            preferences.fontSize = value.font_size;
            for (const auto& shortcut : value.shortcuts)
                preferences.shortcuts.push_back(
                    {string(shortcut.command), string(shortcut.sequence)});
            emit editorPreferencesReady(event.request_token, preferences);
        } else if (kind == "history_listed") {
            QList<SavedHistoryEntry> entries;
            entries.reserve(static_cast<qsizetype>(event.history.size()));
            for (const auto& h : event.history)
                entries.push_back({string(h.id), h.has_profile ? string(h.profile_id) : QString{},
                                   string(h.sql), h.timestamp, h.duration_ms, h.row_count,
                                   string(h.status), h.has_row_count});
            emit historyListed(event.request_token, entries);
        } else if (kind == "history_cleared")
            emit historyCleared(event.request_token);
        else if (kind == "history_policy")
            emit historyPolicyReady(event.request_token, {event.history_policy.enabled,
                                                          event.history_policy.max_age_days,
                                                          event.history_policy.max_records});
        else if (kind == "workspace_restored") {
            QList<SavedEditorDocument> documents;
            documents.reserve(static_cast<qsizetype>(event.documents.size()));
            for (const auto& d : event.documents) {
                documents.push_back({string(d.id), string(d.title), string(d.sql),
                                     d.has_profile ? string(d.profile_id) : QString{},
                                     d.has_file ? string(d.file_path) : QString{}, d.cursor_offset,
                                     d.selection_anchor, d.modified});
            }
            emit workspaceRestored(event.request_token, documents);
        } else if (kind == "workspace_tabs_restored") {
            QList<SavedWorkspaceTab> tabs;
            tabs.reserve(static_cast<qsizetype>(event.workspace_tabs.size()));
            for (const auto& tab : event.workspace_tabs) {
                SavedWorkspaceTab value;
                value.isObject = tab.is_object;
                if (tab.is_object) {
                    value.profileId = string(tab.profile_id);
                    value.objectType = string(tab.object_type);
                    value.objectId = string(tab.object_id);
                    value.label = string(tab.label);
                    value.pane = tab.pane;
                } else {
                    const auto& d = tab.document;
                    value.document = {string(d.id),
                                      string(d.title),
                                      string(d.sql),
                                      d.has_profile ? string(d.profile_id) : QString{},
                                      d.has_file ? string(d.file_path) : QString{},
                                      d.cursor_offset,
                                      d.selection_anchor,
                                      d.modified};
                }
                tabs.append(std::move(value));
            }
            emit workspaceTabsRestored(event.request_token, tabs, event.active_tab);
        } else if (kind == "workspace_saved")
            emit workspaceSaved(event.request_token);
        else if (kind == "recovery_failed")
            emit recoveryFailed(event.request_token, string(event.error));
        else if (kind == "profiles") {
            QList<SavedProfile> profiles;
            profiles.reserve(static_cast<qsizetype>(event.profiles.size()));
            for (const auto& profile : event.profiles)
                profiles.push_back(savedProfile(profile));
            emit profilesReady(event.request_token, profiles);
        } else if (kind == "profile_saved" && event.profiles.size() == 1) {
            emit profileSaved(event.request_token, savedProfile(event.profiles[0]),
                              event.warnings.empty() ? QString{} : string(event.warnings[0]));
        } else if (kind == "profile_deleted")
            emit profileDeleted(event.request_token, string(event.profile_id),
                                event.warnings.empty() ? QString{} : string(event.warnings[0]));
        else if (kind == "profile_tested")
            emit profileTested(event.request_token);
        else if (kind == "profile_failed")
            emit profileFailed(event.request_token,
                               string(event.error) +
                                   (event.vendor_code.empty()
                                        ? QString{}
                                        : tr(" [Code: %1]").arg(string(event.vendor_code))));
    });
    connect(this, &EngineAdapter::recoveryFailed, this,
            [this](quint64 token, const QString& error) {
                if (d_->shutdownToken == token) {
                    d_->shutdownToken.reset();
                    d_->closing = false;
                    emit shutdownFailed(error, true);
                }
            });
    auto* timer = new QTimer(this);
    timer->setInterval(8);
    connect(timer, &QTimer::timeout, this, [this] {
        std::vector<quint64> transfers;
        {
            const auto events = drain_events(*d_->engine);
            for (const auto& event : events) {
                if (event.has_lease) {
                    transfers.push_back(event.lease_id);
                    d_->transfers.insert(event.lease_id,
                                         {event.reserved_bytes, std::nullopt, false});
                }
                emit eventReady(event);
            }
        } // Destroy all CXX strings/vectors before reducing the transfer reservations.
        for (const auto id : transfers) {
            const auto transfer = d_->transfers.take(id);
            if (transfer.retained && !transfer.released) {
                auto result = shrink_page_lease(*d_->engine, id, *transfer.retained);
                if (!result.accepted)
                    emit commandFailed(string(result.error));
            } else {
                auto result = release_page_lease(*d_->engine, id);
                if (!result.accepted)
                    emit commandFailed(string(result.error));
            }
        }
    });
    timer->start();
    const auto error = string(initialization_error(*d_->engine));
    if (!error.isEmpty())
        QTimer::singleShot(0, this, [this, error] { emit commandFailed(error); });
}
EngineAdapter::~EngineAdapter() {
    choscordb::shutdown(*d_->engine);
}
std::optional<quint64> EngineAdapter::connectSqlite(const QString& path, bool readOnly) {
    if (d_->closing || d_->stopping) {
        emit commandFailed(tr("Workspace is closing."));
        return std::nullopt;
    }
    const auto bytes = path.toUtf8();
    auto reply = connect_sqlite(*d_->engine, utf8View(bytes), readOnly);
    if (!reply.accepted) {
        emit commandFailed(string(reply.error));
        return std::nullopt;
    }
    d_->connections.insert(reply.id);
    return reply.id;
}
std::optional<quint64> EngineAdapter::execute(quint64 connection, const QString& sql,
                                              bool autoCommit, const QString& profileId,
                                              const QueryPreferences& preferences) {
    if (d_->closing || d_->stopping) {
        emit commandFailed(tr("Workspace is closing."));
        return std::nullopt;
    }
    const auto limits = queryPreferenceLimits();
    if (preferences.version != limits.version || preferences.pageSize < limits.minPageSize ||
        preferences.pageSize > limits.maxPageSize ||
        preferences.timeoutSeconds > limits.maxTimeoutSeconds) {
        emit commandFailed(tr("Invalid query settings."));
        return std::nullopt;
    }
    const auto bytes = sql.toUtf8(), profileBytes = profileId.toUtf8();
    auto reply = choscordb::execute_with_profile(
        *d_->engine, connection, utf8View(bytes), preferences.pageSize,
        quint64(preferences.timeoutSeconds) * 1000, autoCommit, utf8View(profileBytes));
    if (!reply.accepted) {
        emit commandFailed(string(reply.error));
        return std::nullopt;
    }
    d_->queryPaging.insert(reply.id, {connection, preferences.pageSize});
    return reply.id;
}
TextMatch EngineAdapter::findText(const QString& source, const QString& needle, quint64 start,
                                  bool backwards, bool caseSensitive, bool wholeWord) {
    if (!source.isValidUtf16() || !needle.isValidUtf16()) {
        TextMatch invalid;
        invalid.error = tr("Search input is not valid Unicode.");
        return invalid;
    }
    const auto sourceBytes = source.toUtf8(), needleBytes = needle.toUtf8();
    const auto result = text_find(utf8View(sourceBytes), utf8View(needleBytes), start, backwards,
                                  caseSensitive, wholeWord);
    return {result.valid, result.found, result.wrapped,
            result.start, result.end,   string(result.error)};
}
TextReplacement EngineAdapter::replaceAllText(const QString& source, const QString& needle,
                                              const QString& replacement, bool caseSensitive,
                                              bool wholeWord) {
    if (!source.isValidUtf16() || !needle.isValidUtf16() || !replacement.isValidUtf16()) {
        TextReplacement invalid;
        invalid.error = tr("Search input is not valid Unicode.");
        return invalid;
    }
    const auto sourceBytes = source.toUtf8(), needleBytes = needle.toUtf8(),
               replacementBytes = replacement.toUtf8();
    const auto result = text_replace_all(utf8View(sourceBytes), utf8View(needleBytes),
                                         utf8View(replacementBytes), caseSensitive, wholeWord);
    return {result.valid, string(result.text), string(result.error), result.count};
}
QStringList EngineAdapter::keywordCompletions(const QString& prefix) {
    if (prefix.size() > 256)
        return {};
    const auto bytes = prefix.toUtf8();
    QStringList result;
    for (const auto& item : sql_keyword_completions(utf8View(bytes)))
        result.append(string(item));
    return result;
}
RecoveryLimits EngineAdapter::recoveryLimits() {
    const auto limits = recovery_limits();
    return {limits.max_documents, limits.max_sql_bytes, limits.max_collection_bytes};
}
bool EngineAdapter::queueRecovery(quint64 token, std::function<Submit()> command, quint64 bytes,
                                  bool duringShutdown) {
    if (d_->stopping || (d_->closing && !duringShutdown)) {
        emit recoveryFailed(token, tr("Workspace is closed."));
        return false;
    }
    const auto maximum = recoveryLimits().maxCollectionBytes * 2;
    if (d_->recoveryQueue.size() >= 8 || bytes > maximum - d_->queuedRecoveryBytes) {
        emit recoveryFailed(token, tr("Too many pending history or recovery requests."));
        return false;
    }
    d_->queuedRecoveryBytes += bytes;
    d_->recoveryQueue.enqueue({token, std::move(command), bytes});
    pumpRecovery();
    return true;
}
void EngineAdapter::pumpRecovery() {
    if (d_->stopping || d_->activeRecovery || d_->recoveryQueue.isEmpty())
        return;
    auto request = d_->recoveryQueue.dequeue();
    d_->queuedRecoveryBytes -= request.bytes;
    d_->activeRecovery = request.token;
    const auto result = request.command();
    if (!result.accepted) {
        d_->activeRecovery.reset();
        emit recoveryFailed(request.token, string(result.error));
        QTimer::singleShot(0, this, &EngineAdapter::pumpRecovery);
    }
}
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
void EngineAdapter::listProfiles(quint64 token) {
    auto result = profile_list(*d_->engine, token);
    if (!result.accepted)
        emit profileFailed(token, string(result.error));
}
void EngineAdapter::saveProfile(const SavedProfile& profile, quint64 token) {
    saveProfileWithPassword(profile, {}, "keep", token);
}
void EngineAdapter::saveProfileWithPassword(const SavedProfile& profile, const QString& password,
                                            const QString& action, quint64 token) {
    if (d_->closing || d_->stopping) {
        emit profileFailed(token, tr("Workspace is closing."));
        return;
    }
    auto passwordBytes = password.toUtf8();
    const auto actionBytes = action.toUtf8();
    auto result = profile_save_secret(*d_->engine, profileDto(profile), utf8View(actionBytes),
                                      utf8View(passwordBytes), token);
    passwordBytes.fill(0);
    if (!result.accepted)
        emit profileFailed(token, string(result.error));
}
void EngineAdapter::duplicateProfile(const QString& source, const QString& id, const QString& name,
                                     quint64 token) {
    if (d_->closing || d_->stopping) {
        emit profileFailed(token, tr("Workspace is closing."));
        return;
    }
    const auto sourceBytes = source.toUtf8(), idBytes = id.toUtf8(), nameBytes = name.toUtf8();
    auto result = profile_duplicate(*d_->engine, utf8View(sourceBytes), utf8View(idBytes),
                                    utf8View(nameBytes), token);
    if (!result.accepted)
        emit profileFailed(token, string(result.error));
}
void EngineAdapter::deleteProfile(const QString& id, quint64 token) {
    if (d_->closing || d_->stopping) {
        emit profileFailed(token, tr("Workspace is closing."));
        return;
    }
    const auto bytes = id.toUtf8();
    auto result = profile_delete(*d_->engine, utf8View(bytes), token);
    if (!result.accepted)
        emit profileFailed(token, string(result.error));
}
void EngineAdapter::testProfile(const SavedProfile& profile, quint64 token) {
    testProfileWithPassword(profile, {}, false, token);
}
void EngineAdapter::testProfileWithPassword(const SavedProfile& profile, const QString& password,
                                            bool hasPassword, quint64 token) {
    auto bytes = password.toUtf8();
    auto result =
        profile_test_secret(*d_->engine, profileDto(profile), utf8View(bytes), hasPassword, token);
    bytes.fill(0);
    if (!result.accepted)
        emit profileFailed(token, string(result.error));
}
std::optional<quint64> EngineAdapter::connectProfile(const SavedProfile& profile) {
    return connectProfileWithPassword(profile, {}, false);
}
std::optional<quint64> EngineAdapter::connectProfileWithPassword(const SavedProfile& profile,
                                                                 const QString& password,
                                                                 bool hasPassword) {
    if (d_->closing || d_->stopping) {
        emit profileConnectFailed(tr("Workspace is closing."));
        return std::nullopt;
    }
    auto bytes = password.toUtf8();
    auto result =
        profile_connect_secret(*d_->engine, profileDto(profile), utf8View(bytes), hasPassword);
    bytes.fill(0);
    if (!result.accepted) {
        emit profileConnectFailed(string(result.error));
        return std::nullopt;
    }
    d_->connections.insert(result.id);
    return result.id;
}
quint32 EngineAdapter::pageSizeForQuery(quint64 query) const {
    const auto found = d_->queryPaging.constFind(query);
    return found == d_->queryPaging.cend() ? queryPreferenceLimits().defaultPageSize
                                           : found->pageSize;
}
void EngineAdapter::fetchPage(quint64 query) {
    auto reply = fetch_page(*d_->engine, query, pageSizeForQuery(query));
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
}
void EngineAdapter::fetchPageAt(quint64 query, quint64 index) {
    auto reply = fetch_page_at(*d_->engine, query, index, pageSizeForQuery(query));
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
}
bool EngineAdapter::cancelQuery(quint64 query) {
    auto reply = cancel(*d_->engine, query);
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
    return reply.accepted;
}
std::optional<quint64> EngineAdapter::startExport(quint64 query, const QString& path,
                                                  const QString& format, const QStringList& table,
                                                  bool postgres) {
    rust::Vec<rust::String> parts;
    for (const auto& part : table) {
        const auto bytes = part.toUtf8();
        parts.push_back(rust::String(bytes.constData(), static_cast<size_t>(bytes.size())));
    }
    const auto destination = path.toUtf8();
    const auto encoding = format.toUtf8();
    auto reply = start_export(*d_->engine, query, utf8View(destination), utf8View(encoding),
                              std::move(parts), postgres);
    if (!reply.accepted) {
        emit exportSubmissionFailed(query, string(reply.error));
        return std::nullopt;
    }
    return reply.id;
}
void EngineAdapter::cancelExport(quint64 id) {
    auto reply = cancel_export(*d_->engine, id);
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
}
void EngineAdapter::loadValueChunk(quint64 query, quint64 handle, quint64 offset,
                                   quint32 maxBytes) {
    auto reply = load_value_chunk(*d_->engine, query, handle, offset, maxBytes);
    if (!reply.accepted)
        emit valueChunkSubmissionFailed(query, handle, offset, string(reply.error));
}
void EngineAdapter::loadMetadata(quint64 connection, const QString& parent, quint64 requestToken) {
    const auto bytes = parent.toUtf8();
    auto reply = metadata_request(*d_->engine, connection, utf8View(bytes), requestToken);
    if (!reply.accepted)
        emit metadataSubmissionFailed(connection, parent, requestToken, string(reply.error));
}
std::optional<quint64> EngineAdapter::openObjectData(quint64 connection, const QString& object,
                                                     const QueryPreferences& preferences) {
    if (d_->closing || d_->stopping) {
        emit commandFailed(tr("Workspace is closing."));
        return std::nullopt;
    }
    const auto limits = queryPreferenceLimits();
    if (preferences.version != limits.version || preferences.pageSize < limits.minPageSize ||
        preferences.pageSize > limits.maxPageSize ||
        preferences.timeoutSeconds > limits.maxTimeoutSeconds) {
        emit commandFailed(tr("Invalid query settings."));
        return std::nullopt;
    }
    const auto bytes = object.toUtf8();
    auto reply = open_object_data(*d_->engine, connection, utf8View(bytes), preferences.pageSize,
                                  quint64(preferences.timeoutSeconds) * 1000);
    if (!reply.accepted) {
        emit commandFailed(string(reply.error));
        return std::nullopt;
    }
    d_->queryPaging.insert(reply.id, {connection, preferences.pageSize});
    return reply.id;
}
bool EngineAdapter::inspectEditTarget(quint64 connection, const QString& object, quint64 token) {
    const auto bytes = object.toUtf8();
    auto reply = edit_target_request(*d_->engine, connection, utf8View(bytes), token);
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
    return reply.accepted;
}
bool EngineAdapter::inspectQueryEdit(quint64 connection, const QString& sql,
                                     const QStringList& resultColumns, quint64 token) {
    const auto bytes = sql.toUtf8();
    rust::Vec<rust::String> names;
    for (const auto& name : resultColumns)
        names.push_back(rustString(name));
    auto reply =
        edit_query_request(*d_->engine, connection, utf8View(bytes), std::move(names), token);
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
    return reply.accepted;
}
bool EngineAdapter::applyEditBatch(quint64 connection,
                                   const std::vector<ReviewedEditStatement>& statements,
                                   quint64 token) {
    rust::Vec<EditStatementDto> batch;
    for (const auto& statement : statements) {
        EditStatementDto dto;
        dto.sql = rustString(statement.sql);
        dto.has_expected_rows = statement.expectedRows.has_value();
        dto.expected_rows = statement.expectedRows.value_or(0);
        for (size_t i = 0; i < statement.params.size(); ++i) {
            const auto& value = statement.params[i];
            const auto type =
                i < statement.paramTypes.size() ? statement.paramTypes[i].toLower() : QString{};
            CellDto cell;
            if (std::holds_alternative<std::monostate>(value))
                cell.kind = "null";
            else if (const auto* v = std::get_if<bool>(&value)) {
                cell.kind = "boolean";
                cell.boolean = *v;
            } else if (const auto* v = std::get_if<qint64>(&value)) {
                cell.kind = "integer";
                cell.integer = *v;
            } else if (const auto* v = std::get_if<double>(&value)) {
                cell.kind = "real";
                cell.real = *v;
            } else if (const auto* v = std::get_if<QString>(&value)) {
                cell.kind = type.startsWith("numeric") || type.startsWith("decimal") ? "decimal"
                            : type == "date"                                         ? "date"
                            : type == "time" || type.startsWith("time ")             ? "time"
                            : type.startsWith("timestamp")                           ? "timestamp"
                            : type == "uuid"                                         ? "uuid"
                            : type == "json" || type == "jsonb"                      ? "json"
                                                                                     : "text";
                cell.text = rustString(*v);
            } else if (const auto* v = std::get_if<QByteArray>(&value)) {
                cell.kind = "binary";
                for (char byte : *v)
                    cell.bytes.push_back(static_cast<uint8_t>(byte));
            } else {
                emit commandFailed(tr("Deferred values cannot be bound to grid edits."));
                return false;
            }
            dto.params.push_back(std::move(cell));
        }
        batch.push_back(std::move(dto));
    }
    auto reply = apply_edit_batch(*d_->engine, connection, std::move(batch), token);
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
    return reply.accepted;
}
void EngineAdapter::loadObjectInspection(quint64 connection, const QString& object,
                                         ObjectInspectionPane pane, quint64 requestToken) {
    // Each pane retains only its newest request; late responses cannot populate a new context.
    for (auto it = d_->inspections.begin(); it != d_->inspections.end();) {
        if (it->connection == connection && it->pane == pane)
            it = d_->inspections.erase(it);
        else
            ++it;
    }
    if (d_->inspections.size() >= 64) {
        emit objectInspectionFailed(
            connection, object, requestToken,
            tr("Too many pending metadata requests; retry after loading completes"));
        return;
    }
    const auto token = d_->nextInspectionToken++;
    const auto bytes = object.toUtf8();
    d_->inspections.insert(token, {connection, requestToken, object, pane});
    auto reply = pane == ObjectInspectionPane::Ddl
                     ? object_ddl_request(*d_->engine, connection, utf8View(bytes), token)
                     : metadata_request(*d_->engine, connection, utf8View(bytes), token);
    if (!reply.accepted) {
        d_->inspections.remove(token);
        emit objectInspectionFailed(connection, object, requestToken, string(reply.error));
    }
}
void EngineAdapter::objectDdl(quint64 connection, const QString& object) {
    const auto bytes = object.toUtf8();
    auto reply = object_ddl(*d_->engine, connection, utf8View(bytes));
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
}
void EngineAdapter::commitTransaction(quint64 connection) {
    auto reply = commit(*d_->engine, connection);
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
}
void EngineAdapter::rollbackTransaction(quint64 connection) {
    auto reply = rollback(*d_->engine, connection);
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
}
bool EngineAdapter::disconnectConnection(quint64 connection) {
    auto reply = choscordb::disconnect(*d_->engine, connection);
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
    return reply.accepted;
}
void EngineAdapter::releaseQuery(quint64 query) {
    auto reply = release_query(*d_->engine, query);
    if (reply.accepted)
        d_->queryPaging.remove(query);
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
}
void EngineAdapter::beginShutdown() {
    if (d_->closing || d_->stopping)
        return;
    d_->closing = true;
    const auto connections = d_->connections;
    for (const auto id : connections)
        choscordb::disconnect(*d_->engine, id);
    finishShutdown();
}
void EngineAdapter::finishShutdown() {
    if (!d_->closing || !d_->connections.isEmpty() || d_->shutdownToken || d_->stopping)
        return;
    static quint64 token = quint64(1) << 59;
    d_->shutdownToken = ++token;
    queueRecovery(
        *d_->shutdownToken,
        [this, token = *d_->shutdownToken] { return history_flush(*d_->engine, token); }, 0, true);
}
void EngineAdapter::shutdown() {
    d_->stopping = true;
    d_->recoveryQueue.clear();
    d_->queryPaging.clear();
    d_->queuedRecoveryBytes = 0;
    d_->activeRecovery.reset();
    choscordb::shutdown(*d_->engine);
}
bool EngineAdapter::retainTransfer(quint64 lease, quint64 payloadBytes) {
    auto transfer = d_->transfers.find(lease);
    if (transfer == d_->transfers.end() || transfer->released || transfer->retained ||
        transfer->reserved < 256 || payloadBytes > transfer->reserved - 256)
        return false;
    transfer->retained = payloadBytes;
    return true;
}
void EngineAdapter::releasePageLease(quint64 lease) {
    auto transfer = d_->transfers.find(lease);
    if (transfer != d_->transfers.end()) {
        // A nested Qt event loop may replace the view before the outer DTO dies.
        transfer->released = true;
        return;
    }
    auto result = release_page_lease(*d_->engine, lease);
    if (!result.accepted)
        emit commandFailed(string(result.error));
}
PageMemoryUsage EngineAdapter::memoryUsage() const {
    const auto usage = memory_usage(*d_->engine);
    return {usage.used_bytes, usage.source_bytes, usage.peak_bytes};
}
PageCacheUsage EngineAdapter::cacheUsage() const {
    const auto usage = cache_usage(*d_->engine);
    return {usage.hits, usage.misses, usage.resident_bytes};
}
SqlSelection EngineAdapter::executionRange(const QString& sql, quint64 cursor, quint64 start,
                                           quint64 end) {
    const auto bytes = sql.toUtf8();
    const auto range = sql_execution_range(utf8View(bytes), cursor, start, end);
    return {range.valid, range.start, range.end, range.confirmation_required};
}
} // namespace choscordb
