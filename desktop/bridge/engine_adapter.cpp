#include "bridge/engine_adapter_p.h"
#include "bridge/request_token.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <algorithm>
#include <vector>
namespace choscordb {
using engine_adapter_detail::fromRust;
using engine_adapter_detail::objectGraph;
using engine_adapter_detail::profileDto;
using engine_adapter_detail::secretDraftsDto;
using engine_adapter_detail::toRust;
using engine_adapter_detail::utf8View;
namespace {
ProfileDto withConnectionTimeout(const SavedProfile& profile, quint32 timeout) {
    auto dto = profileDto(profile);
    dto.session_connection_timeout_seconds = timeout;
    return dto;
}
SavedProfile savedProfile(const ProfileDto& dto) {
    SavedProfile value;
    value.groupId = fromRust(dto.group_id);
    value.id = fromRust(dto.id);
    value.name = fromRust(dto.name);
    value.driver = fromRust(dto.driver);
    value.path = fromRust(dto.path);
    value.readOnly = dto.read_only;
    value.host = fromRust(dto.host);
    value.port = dto.port;
    value.database = fromRust(dto.database);
    value.user = fromRust(dto.user);
    value.tls = fromRust(dto.tls);
    value.rootCertificate = fromRust(dto.root_certificate);
    value.tlsClientIdentity = fromRust(dto.tls_client_identity);
    value.tlsCredentialRef = fromRust(dto.tls_credential_ref);
    value.proxyOptions = fromRust(dto.proxy_options);
    value.proxyCredentialRef = fromRust(dto.proxy_credential_ref);
    value.sshJumpCredentialRefs = fromRust(dto.ssh_jump_credential_refs);
    value.sshPrivateKeyRef = fromRust(dto.ssh_private_key_ref);
    value.sshJumpPrivateKeyRefs = fromRust(dto.ssh_jump_private_key_refs);
    value.sshOptions = fromRust(dto.ssh_options);
    value.credentialRef = fromRust(dto.credential_ref);
    value.sshCredentialRef = fromRust(dto.ssh_credential_ref);
    value.sshEnabled = dto.ssh_enabled;
    value.sshHost = fromRust(dto.ssh_host);
    value.sshPort = dto.ssh_port;
    value.sshUser = fromRust(dto.ssh_user);
    value.sshAuthentication = fromRust(dto.ssh_authentication);
    value.sshIdentitySource = fromRust(dto.ssh_identity_source);
    value.sshIdentityFile = fromRust(dto.ssh_identity_file);
    return value;
}
void assignAppearance(AppearanceLayout& value, const AppearanceLayoutDto& dto) {
    value.version = dto.version;
    value.theme = fromRust(dto.theme);
    value.navigatorWidth = dto.navigator_width;
    value.historyHeight = dto.history_height;
    value.editorResultsSplit = dto.editor_results_split;
    value.navigatorVisible = dto.navigator_visible;
    value.historyVisible = dto.history_visible;
    value.x = dto.x;
    value.y = dto.y;
    value.width = dto.width;
    value.height = dto.height;
    value.maximized = dto.maximized;
    value.hasScreenName = dto.has_screen_name;
    value.screenName = fromRust(dto.screen_name);
}
AppearanceLayout appearanceLayout(const AppearanceLayoutDto& dto) {
    AppearanceLayout value;
    assignAppearance(value, dto);
    return value;
}
} // namespace
AppearanceLayout::AppearanceLayout() {
    assignAppearance(*this, appearance_layout_default());
}
EngineAdapter::Private::Private(const QString& path)
    : engine(path.isEmpty() ? new_engine() : new_engine_with_storage(utf8View(path.toUtf8()))),
      connectionTimeoutSeconds(query_preference_limits().default_connection_timeout_seconds) {}
EngineAdapter::EngineAdapter(QObject* parent, const QString& storagePath)
    : QObject(parent), d_(std::make_unique<Private>(storagePath)) {
    trackHistoryClears();
    connect(this, &EngineAdapter::eventReady, this, [this](const BridgeEvent& event) {
        const auto kind = fromRust(event.kind);
        if (kind == "object_graph" || kind == "object_graph_failed") {
            const auto it = d_->graphs.find(event.request_token);
            if (it != d_->graphs.end() && it->connection == event.id &&
                it->object == fromRust(event.object)) {
                const auto request = it.value();
                d_->graphs.erase(it);
                if (kind == "object_graph_failed") {
                    if (fromRust(event.error_kind) == "Unsupported") {
                        ObjectGraph graph;
                        graph.availability = MetadataAvailability::Unsupported;
                        graph.reason = fromRust(event.error);
                        emit objectGraphReady(request.connection, request.object, request.token,
                                              graph);
                    } else {
                        emit objectGraphFailed(request.connection, request.object, request.token,
                                               fromRust(event.error));
                    }
                } else {
                    emit objectGraphReady(request.connection, request.object, request.token,
                                          objectGraph(event.graph));
                }
            }
        }
        if (kind == "metadata" || kind == "metadata_failed" || kind == "ddl" ||
            kind == "ddl_failed") {
            const auto it = d_->inspections.find(event.request_token);
            if (it != d_->inspections.end() && it->connection == event.id &&
                it->object ==
                    (kind.startsWith("ddl") ? fromRust(event.object) : fromRust(event.parent))) {
                const auto request = it.value();
                d_->inspections.erase(it);
                ObjectInspection result;
                result.pane = request.pane;
                if (kind.endsWith("_failed")) {
                    if (fromRust(event.error_kind) == "Unsupported") {
                        result.availability = MetadataAvailability::Unsupported;
                        result.reason = fromRust(event.error);
                        emit objectInspectionReady(request.connection, request.object,
                                                   request.token, result);
                    } else {
                        emit objectInspectionFailed(request.connection, request.object,
                                                    request.token, fromRust(event.error));
                    }
                } else {
                    result.ddl = fromRust(event.ddl);
                    for (const auto& object : event.objects) {
                        const auto objectKind = fromRust(object.kind);
                        if (objectKindTraits(objectKind).detailPane != int(request.pane))
                            continue;
                        ObjectInspectionRow row;
                        row.id = fromRust(object.id);
                        row.name = fromRust(object.name);
                        row.kind = objectKind;
                        row.primaryKey = object.primary_key;
                        if (object.has_column) {
                            row.properties.append({tr("Type"),
                                                   fromRust(object.column.database_type),
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
                            const auto availability = fromRust(property.availability);
                            row.properties.append(
                                {fromRust(property.name), fromRust(property.value),
                                 availability == "unsupported"   ? MetadataAvailability::Unsupported
                                 : availability == "unavailable" ? MetadataAvailability::Unavailable
                                                                 : MetadataAvailability::Available,
                                 fromRust(property.reason)});
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
            for (auto it = d_->graphs.begin(); it != d_->graphs.end();) {
                if (it->connection == event.id) {
                    const auto request = it.value();
                    it = d_->graphs.erase(it);
                    emit objectGraphFailed(request.connection, request.object, request.token,
                                           tr("Connection disconnected"));
                } else
                    ++it;
            }
            for (auto it = d_->inspections.begin(); it != d_->inspections.end();) {
                if (it->connection == event.id) {
                    const auto request = it.value();
                    it = d_->inspections.erase(it);
                    emit objectInspectionFailed(request.connection, request.object, request.token,
                                                tr("Connection disconnected"));
                } else
                    ++it;
            }
            if (d_->closing)
                QTimer::singleShot(0, this, &EngineAdapter::finishShutdown);
        }
        if (kind == "history_write_failed") {
            if (d_->closing)
                d_->shutdownHistoryError = fromRust(event.error);
            emit historyWriteFailed(event.id, fromRust(event.error));
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
        if (kind == "query_preferences") {
            QueryPreferences preferences;
            preferences.version = event.query_preferences.version;
            preferences.pageSize = event.query_preferences.page_size;
            preferences.timeoutSeconds = event.query_preferences.timeout_seconds;
            preferences.connectionTimeoutSeconds =
                event.query_preferences.connection_timeout_seconds;
            preferences.showSystemSchemas = event.query_preferences.show_system_schemas;
            // Rust validated the stored preferences before reporting them.
            d_->connectionTimeoutSeconds = preferences.connectionTimeoutSeconds;
            emit queryPreferencesReady(event.request_token, preferences);
        } else if (kind == "appearance_layout") {
            emit appearanceLayoutReady(event.request_token, event.has_appearance,
                                       appearanceLayout(event.appearance_layout));
        } else if (kind == "editor_preferences") {
            const auto& value = event.editor_preferences;
            EditorPreferences preferences;
            preferences.version = value.version;
            preferences.fontFamily = fromRust(value.font_family);
            preferences.fontSize = value.font_size;
            for (const auto& shortcut : value.shortcuts)
                preferences.shortcuts.push_back(
                    {fromRust(shortcut.command), fromRust(shortcut.sequence)});
            emit editorPreferencesReady(event.request_token, preferences);
        } else if (kind == "history_listed" || kind == "history_searched") {
            QList<SavedHistoryEntry> entries;
            entries.reserve(static_cast<qsizetype>(event.history.size()));
            for (const auto& h : event.history)
                entries.push_back({fromRust(h.id),
                                   h.has_profile ? fromRust(h.profile_id) : QString{},
                                   fromRust(h.sql), h.timestamp, h.duration_ms, h.row_count,
                                   fromRust(h.status), h.has_row_count});
            if (kind == "history_listed")
                emit historyListed(event.request_token, entries);
            else
                emit historySearched(event.request_token, entries, event.history_incomplete,
                                     event.history_next_offset);
        } else if (kind == "history_cleared")
            emit historyCleared(event.request_token);
        else if (kind == "history_policy") {
            HistoryPolicy policy;
            policy.enabled = event.history_policy.enabled;
            policy.maxAgeDays = event.history_policy.max_age_days;
            policy.maxRecords = event.history_policy.max_records;
            emit historyPolicyReady(event.request_token, policy);
        } else if (kind == "workspace_tabs_restored") {
            QList<SavedWorkspaceTab> tabs;
            tabs.reserve(static_cast<qsizetype>(event.workspace_tabs.size()));
            for (const auto& tab : event.workspace_tabs) {
                SavedWorkspaceTab value;
                value.isObject = tab.is_object;
                if (tab.is_object) {
                    value.profileId = fromRust(tab.profile_id);
                    value.objectType = fromRust(tab.object_type);
                    value.objectId = fromRust(tab.object_id);
                    value.label = fromRust(tab.label);
                    value.pane = tab.pane;
                } else {
                    const auto& d = tab.document;
                    value.document = {fromRust(d.id),
                                      fromRust(d.title),
                                      fromRust(d.sql),
                                      d.has_profile ? fromRust(d.profile_id) : QString{},
                                      d.has_file ? fromRust(d.file_path) : QString{},
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
            emit recoveryFailed(event.request_token, fromRust(event.error));
        else if (kind == "profiles") {
            QList<SavedProfile> profiles;
            profiles.reserve(static_cast<qsizetype>(event.profiles.size()));
            for (const auto& profile : event.profiles)
                profiles.push_back(savedProfile(profile));
            emit profilesReady(event.request_token, profiles);
        } else if (kind == "profile_saved" && event.profiles.size() == 1) {
            emit profileSaved(event.request_token, savedProfile(event.profiles[0]),
                              event.warnings.empty() ? QString{} : fromRust(event.warnings[0]));
        } else if (kind == "profile_deleted")
            emit profileDeleted(event.request_token, fromRust(event.profile_id),
                                event.warnings.empty() ? QString{} : fromRust(event.warnings[0]));
        else if (kind == "profile_tested")
            emit profileTested(event.request_token);
        else if (kind == "profile_failed")
            emit profileFailed(event.request_token,
                               fromRust(event.error) +
                                   (event.vendor_code.empty()
                                        ? QString{}
                                        : tr(" [Code: %1]").arg(fromRust(event.vendor_code))));
        else if (kind == "ssh_host_keys_inspected")
            emit sshHostKeysInspected(event.request_token, engine_adapter_detail::hostKeyCandidates(
                                                               event.host_key_candidates));
        else if (kind == "ssh_host_key_approved")
            emit sshHostKeyApproved(event.request_token, fromRust(event.host_key_approval));
        else if (kind == "ssh_host_key_failed")
            emit sshHostKeyOperationFailed(event.request_token, fromRust(event.error));
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
                    emit commandFailed(fromRust(result.error));
            } else {
                auto result = release_page_lease(*d_->engine, id);
                if (!result.accepted)
                    emit commandFailed(fromRust(result.error));
            }
        }
    });
    timer->start();
    const auto error = fromRust(initialization_error(*d_->engine));
    if (!error.isEmpty())
        QTimer::singleShot(0, this, [this, error] { emit commandFailed(error); });
}
EngineAdapter::~EngineAdapter() {
    choscordb::shutdown(*d_->engine);
}
void EngineAdapter::attachDiagnostics(const RustDiagnostics& service) {
    diagnostics_attach_engine(*d_->engine, service);
}
std::optional<quint64> EngineAdapter::connectSqlite(const QString& path, bool readOnly) {
    if (d_->closing || d_->stopping) {
        emit commandFailed(tr("Workspace is closing."));
        return std::nullopt;
    }
    const auto bytes = path.toUtf8();
    auto reply = connect_sqlite(*d_->engine, utf8View(bytes), readOnly);
    if (!reply.accepted) {
        emit commandFailed(fromRust(reply.error));
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
    const auto bytes = sql.toUtf8(), profileBytes = profileId.toUtf8();
    auto reply = choscordb::execute_with_profile(
        *d_->engine, connection, utf8View(bytes), preferences.pageSize,
        quint64(preferences.timeoutSeconds) * 1000, autoCommit, utf8View(profileBytes));
    if (!reply.accepted) {
        emit commandFailed(fromRust(reply.error));
        return std::nullopt;
    }
    return reply.id;
}
namespace {
SearchInput searchField(TextSearchField field) {
    switch (field) {
    case TextSearchField::Needle:
        return SearchInput::Needle;
    case TextSearchField::Replacement:
        return SearchInput::Replacement;
    default:
        return SearchInput::General;
    }
}
} // namespace
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
    return {result.valid,
            result.found,
            result.wrapped,
            result.start,
            result.end,
            fromRust(result.error),
            searchField(result.error_field)};
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
    return {result.valid, fromRust(result.text), fromRust(result.error),
            searchField(result.error_field), result.count};
}
bool EngineAdapter::searchPatternUsable(const QString& needle) {
    const auto bytes = needle.toUtf8();
    return needle.isValidUtf16() && text_pattern_usable(utf8View(bytes));
}
const TextLimits& EngineAdapter::textLimits() {
    static const TextLimits limits = [] {
        const auto dto = text_limits();
        return TextLimits{dto.max_document_bytes, dto.max_search_pattern_bytes};
    }();
    return limits;
}
QString EngineAdapter::replacementError(quint64 documentBytes, quint64 removedBytes,
                                        quint64 addedBytes) {
    return fromRust(text_replacement_error(documentBytes, removedBytes, addedBytes));
}
QStringList EngineAdapter::keywordCompletions(const QString& prefix) {
    const auto bytes = prefix.toUtf8();
    QStringList result;
    for (const auto& item : sql_keyword_completions(utf8View(bytes)))
        result.append(fromRust(item));
    return result;
}
bool EngineAdapter::submitRecovery(quint64 token, const std::function<Submit()>& command,
                                   bool duringShutdown) {
    if (d_->stopping || (d_->closing && !duringShutdown)) {
        emit recoveryFailed(token, tr("Workspace is closed."));
        return false;
    }
    const auto result = command();
    if (!result.accepted)
        emit recoveryFailed(token, fromRust(result.error));
    return result.accepted;
}
void EngineAdapter::listProfiles(quint64 token) {
    auto result = profile_list(*d_->engine, token);
    if (!result.accepted)
        emit profileFailed(token, fromRust(result.error));
}
void EngineAdapter::saveProfile(const SavedProfile& profile, quint64 token) {
    ProfileSecretDrafts secrets;
    secrets.saveCredentials = true;
    saveProfileDraft(profile, secrets, token);
}
void EngineAdapter::saveProfileDraft(const SavedProfile& profile,
                                     const ProfileSecretDrafts& secrets, quint64 token) {
    if (d_->closing || d_->stopping) {
        emit profileFailed(token, tr("Workspace is closing."));
        return;
    }
    auto dto = profileDto(profile);
    auto credentials = profile_draft_credentials(dto, secretDraftsDto(secrets), true);
    auto result =
        profile_save_credentials(*d_->engine, std::move(dto), std::move(credentials), token);
    if (!result.accepted)
        emit profileFailed(token, fromRust(result.error));
}
void EngineAdapter::duplicateProfile(const QString& source, const QString& name, quint64 token) {
    if (d_->closing || d_->stopping) {
        emit profileFailed(token, tr("Workspace is closing."));
        return;
    }
    const auto sourceBytes = source.toUtf8(), nameBytes = name.toUtf8();
    auto result = profile_duplicate(*d_->engine, utf8View(sourceBytes), utf8View(nameBytes), token);
    if (!result.accepted)
        emit profileFailed(token, fromRust(result.error));
}
void EngineAdapter::deleteProfile(const QString& id, quint64 token) {
    if (d_->closing || d_->stopping) {
        emit profileFailed(token, tr("Workspace is closing."));
        return;
    }
    const auto bytes = id.toUtf8();
    auto result = profile_delete(*d_->engine, utf8View(bytes), token);
    if (!result.accepted)
        emit profileFailed(token, fromRust(result.error));
}
void EngineAdapter::testProfileDraft(const SavedProfile& profile,
                                     const ProfileSecretDrafts& secrets, quint64 token) {
    auto credentials =
        profile_draft_credentials(profileDto(profile), secretDraftsDto(secrets), false);
    auto result = profile_test_credentials(
        *d_->engine, withConnectionTimeout(profile, d_->connectionTimeoutSeconds),
        std::move(credentials), token);
    if (!result.accepted)
        emit profileFailed(token, fromRust(result.error));
}
std::optional<quint64> EngineAdapter::connectProfile(const SavedProfile& profile) {
    return connectProfileDraft(profile, {});
}
std::optional<quint64> EngineAdapter::connectProfileDraft(const SavedProfile& profile,
                                                          const ProfileSecretDrafts& secrets) {
    if (d_->closing || d_->stopping) {
        emit profileConnectFailed(tr("Workspace is closing."));
        return std::nullopt;
    }
    auto credentials =
        profile_draft_credentials(profileDto(profile), secretDraftsDto(secrets), false);
    auto result = profile_connect_credentials(
        *d_->engine, withConnectionTimeout(profile, d_->connectionTimeoutSeconds),
        std::move(credentials));
    if (!result.accepted) {
        emit profileConnectFailed(fromRust(result.error));
        return std::nullopt;
    }
    d_->connections.insert(result.id);
    return result.id;
}
SavedProfile EngineAdapter::profileDraftDefaults(const SavedProfile& profile) {
    return savedProfile(profile_draft_defaults(profileDto(profile)));
}
SavedProfile EngineAdapter::normalizeProfileDraft(const SavedProfile& original,
                                                  const SavedProfile& edited,
                                                  const ProfileSecretDrafts& secrets) {
    return savedProfile(profile_draft_normalize(profileDto(original), profileDto(edited),
                                                secretDraftsDto(secrets)));
}
ProfileFieldError EngineAdapter::validateProfileDraft(const SavedProfile& profile,
                                                      const ProfileSecretDrafts& secrets) {
    const auto error = profile_draft_validate(profileDto(profile), secretDraftsDto(secrets));
    return {fromRust(error.field), fromRust(error.message)};
}
quint16 EngineAdapter::profilePortForDriver(quint16 port, const QString& driver) {
    const auto bytes = driver.toUtf8();
    return profile_port_for_driver(port, utf8View(bytes));
}
ProfileDriverForm EngineAdapter::profileDriverForm(const QString& driver) {
    const auto bytes = driver.toUtf8();
    const auto form = profile_driver_form(utf8View(bytes));
    return {form.server, form.user_optional, form.database_selects_server};
}
bool EngineAdapter::profileHasSavedCredentials(const SavedProfile& profile) {
    return profile_has_saved_credentials(profileDto(profile));
}
void EngineAdapter::fetchPage(quint64 query) {
    auto reply = fetch_page(*d_->engine, query);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
}
void EngineAdapter::fetchPageAt(quint64 query, quint64 index) {
    auto reply = fetch_page_at(*d_->engine, query, index);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
}
bool EngineAdapter::applyResultView(quint64 query, const QList<ResultFilterCondition>& filters,
                                    qint32 sortColumn, const QString& sortDirection) {
    rust::Vec<ResultFilterDto> values;
    values.reserve(static_cast<size_t>(filters.size()));
    for (const auto& filter : filters) {
        ResultFilterDto value;
        value.column = filter.column;
        value.operation = toRust(filter.operation);
        value.value_kind = toRust(filter.valueKind);
        value.value = toRust(filter.value);
        values.push_back(std::move(value));
    }
    const auto direction = sortDirection.toUtf8();
    auto reply = apply_result_view(*d_->engine, query, std::move(values),
                                   sortColumn < 0 ? 0u : static_cast<quint32>(sortColumn),
                                   utf8View(direction));
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
    return reply.accepted;
}
bool EngineAdapter::cancelResultView(quint64 query) {
    auto reply = cancel_result_view(*d_->engine, query);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
    return reply.accepted;
}
bool EngineAdapter::clearResultView(quint64 query) {
    auto reply = clear_result_view(*d_->engine, query);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
    return reply.accepted;
}
bool EngineAdapter::cancelQuery(quint64 query) {
    auto reply = cancel(*d_->engine, query);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
    return reply.accepted;
}
std::optional<quint64> EngineAdapter::startExportDialect(quint64 query, const QString& path,
                                                         const QString& format,
                                                         const QStringList& table,
                                                         const QString& dialect) {
    rust::Vec<rust::String> parts;
    for (const auto& part : table)
        parts.push_back(toRust(part));
    const auto destination = path.toUtf8();
    const auto encoding = format.toUtf8();
    const auto dialectBytes = dialect.toUtf8();
    auto reply = start_export_dialect(*d_->engine, query, utf8View(destination), utf8View(encoding),
                                      std::move(parts), utf8View(dialectBytes));
    if (!reply.accepted) {
        emit exportSubmissionFailed(query, fromRust(reply.error));
        return std::nullopt;
    }
    return reply.id;
}
void EngineAdapter::cancelExport(quint64 id) {
    auto reply = cancel_export(*d_->engine, id);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
}
QString EngineAdapter::valueDetailError(quint64 bytes) {
    return fromRust(value_detail_error(bytes));
}
quint32 EngineAdapter::valueChunkBytes() {
    static const quint32 bytes = value_chunk_max_bytes();
    return bytes;
}
void EngineAdapter::loadValueChunk(quint64 query, quint64 handle, quint64 offset,
                                   quint32 maxBytes) {
    auto reply = load_value_chunk(*d_->engine, query, handle, offset, maxBytes);
    if (!reply.accepted)
        emit valueChunkSubmissionFailed(query, handle, offset, fromRust(reply.error));
}
bool EngineAdapter::refreshSqlMode(quint64 connection, quint64 requestToken) {
    auto reply = refresh_sql_mode(*d_->engine, connection, requestToken);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
    return reply.accepted;
}
void EngineAdapter::nextResultSet(quint64 query) {
    auto reply = next_result_set(*d_->engine, query);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
}
void EngineAdapter::loadMetadataPage(quint64 connection, const QString& parent,
                                     quint64 requestToken, quint64 offset) {
    const auto bytes = parent.toUtf8();
    auto reply =
        metadata_page_request(*d_->engine, connection, utf8View(bytes), requestToken, offset);
    if (!reply.accepted)
        emit metadataSubmissionFailed(connection, parent, requestToken, fromRust(reply.error));
}
void EngineAdapter::loadMetadata(quint64 connection, const QString& parent, quint64 requestToken) {
    loadMetadataPage(connection, parent, requestToken, 0);
}
std::optional<quint64> EngineAdapter::openObjectData(quint64 connection, const QString& object,
                                                     const QueryPreferences& preferences) {
    if (d_->closing || d_->stopping) {
        emit commandFailed(tr("Workspace is closing."));
        return std::nullopt;
    }
    const auto bytes = object.toUtf8();
    auto reply = open_object_data(*d_->engine, connection, utf8View(bytes), preferences.pageSize,
                                  quint64(preferences.timeoutSeconds) * 1000);
    if (!reply.accepted) {
        emit commandFailed(fromRust(reply.error));
        return std::nullopt;
    }
    return reply.id;
}
bool EngineAdapter::inspectEditTarget(quint64 connection, const QString& object, quint64 token) {
    const auto bytes = object.toUtf8();
    auto reply = edit_target_request(*d_->engine, connection, utf8View(bytes), token);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
    return reply.accepted;
}
bool EngineAdapter::inspectQueryEdit(quint64 connection, const QString& sql,
                                     const QStringList& resultColumns, quint64 token) {
    const auto bytes = sql.toUtf8();
    rust::Vec<rust::String> names;
    for (const auto& name : resultColumns)
        names.push_back(toRust(name));
    auto reply =
        edit_query_request(*d_->engine, connection, utf8View(bytes), std::move(names), token);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
    return reply.accepted;
}
bool EngineAdapter::inspectResultCells(quint64 connection, const QString& object,
                                       const QString& sql, const QStringList& resultColumns,
                                       quint64 token) {
    rust::Vec<rust::String> names;
    for (const auto& name : resultColumns)
        names.push_back(toRust(name));
    const auto objectBytes = object.toUtf8();
    const auto sqlBytes = sql.toUtf8();
    auto reply = result_cells_request(*d_->engine, connection, utf8View(objectBytes),
                                      utf8View(sqlBytes), std::move(names), token);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
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
    const auto token = d_->nextInspectionToken++;
    const auto bytes = object.toUtf8();
    d_->inspections.insert(token, {connection, requestToken, object, pane});
    auto reply = pane == ObjectInspectionPane::Ddl
                     ? object_ddl_request(*d_->engine, connection, utf8View(bytes), token)
                     : metadata_request(*d_->engine, connection, utf8View(bytes), token);
    if (!reply.accepted) {
        d_->inspections.remove(token);
        emit objectInspectionFailed(connection, object, requestToken, fromRust(reply.error));
    }
}
void EngineAdapter::objectDdl(quint64 connection, const QString& object) {
    const auto bytes = object.toUtf8();
    auto reply = object_ddl(*d_->engine, connection, utf8View(bytes));
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
}
void EngineAdapter::commitTransaction(quint64 connection) {
    auto reply = commit(*d_->engine, connection);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
}
void EngineAdapter::rollbackTransaction(quint64 connection) {
    auto reply = rollback(*d_->engine, connection);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
}
bool EngineAdapter::disconnectConnection(quint64 connection) {
    auto reply = choscordb::disconnect(*d_->engine, connection);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
    return reply.accepted;
}
void EngineAdapter::releaseQuery(quint64 query) {
    auto reply = release_query(*d_->engine, query);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
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
    d_->shutdownToken = nextRequestToken();
    submitRecovery(
        *d_->shutdownToken,
        [this, token = *d_->shutdownToken] { return history_flush(*d_->engine, token); }, true);
}
void EngineAdapter::shutdown() {
    d_->stopping = true;
    choscordb::shutdown(*d_->engine);
}
bool EngineAdapter::retainTransfer(quint64 lease, quint64 payloadBytes) {
    auto transfer = d_->transfers.find(lease);
    if (transfer == d_->transfers.end() || transfer->released || transfer->retained ||
        !page_lease_fits(transfer->reserved, payloadBytes))
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
        emit commandFailed(fromRust(result.error));
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
                                           quint64 end, const QString& driver,
                                           const QString& sqlMode) {
    const auto bytes = sql.toUtf8();
    const auto driverBytes = driver.toUtf8();
    const auto modeBytes = sqlMode.toUtf8();
    const auto range = sql_execution_range(utf8View(bytes), cursor, start, end,
                                           utf8View(driverBytes), utf8View(modeBytes));
    return {range.valid, range.start, range.end, range.confirmation_required};
}
} // namespace choscordb
