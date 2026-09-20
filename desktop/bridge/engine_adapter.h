#pragma once
#include "models/result_table_model.h"
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
namespace choscordb {
struct BridgeEvent;
struct ReviewedEditStatement {
    QString sql;
    std::vector<Cell> params;
    std::vector<QString> paramTypes;
    std::optional<quint64> expectedRows;
};
struct Submit;
struct SavedProfile {
    QString id, name, groupId, driver = "sqlite", path, host, database, user;
    QString tls = "verify_full", rootCertificate, credentialRef;
    bool readOnly = false;
    quint16 port = 5432;
    bool sshEnabled = false;
    QString sshHost, sshUser, sshIdentityFile;
    quint16 sshPort = 22;
};
struct SavedHistoryEntry {
    QString id, profileId, sql;
    qint64 timestamp = 0;
    quint64 durationMs = 0, rowCount = 0;
    QString status;
    bool hasRowCount = false;
};
struct HistoryPolicy {
    bool enabled = true;
    quint32 maxAgeDays = 90, maxRecords = 10000;
};
struct ShortcutOverride {
    QString command, sequence;
};
struct EditorPreferences {
    quint32 version = 1;
    QString fontFamily;
    quint16 fontSize = 13;
    QList<ShortcutOverride> shortcuts;
};
struct EditorPreferenceLimits {
    quint32 version;
    quint16 defaultFontSize, minFontSize, maxFontSize;
};
struct QueryPreferences {
    QueryPreferences();
    quint32 version, pageSize, timeoutSeconds;
};
struct QueryPreferenceLimits {
    quint32 version, minPageSize, maxPageSize, defaultPageSize, maxTimeoutSeconds;
};
struct AppearanceLayout {
    quint32 version = 1;
    QString theme = "system", density = "compact", accentKind = "preset", accent = "cobalt";
    quint32 navigatorWidth = 280, historyHeight = 220;
    quint16 editorResultsSplit = 600;
    bool navigatorVisible = true, historyVisible = false;
    qint32 x = 0, y = 0;
    quint32 width = 1280, height = 900;
    bool maximized = false, hasScreenName = false;
    QString screenName;
};
struct RecoveryLimits {
    quint64 maxDocuments, maxSqlBytes, maxCollectionBytes;
};
struct SavedEditorDocument {
    QString id, title, sql, profileId, filePath;
    quint64 cursorOffset = 0, selectionAnchor = 0;
    bool modified = false;
};
struct SavedWorkspaceTab {
    bool isObject = false;
    SavedEditorDocument document;
    QString profileId, objectType, objectId, label;
    quint32 pane = 0;
};
struct PageCacheUsage {
    quint64 hits;
    quint64 misses;
    quint64 residentBytes;
};
struct PageMemoryUsage {
    quint64 used;
    quint64 source;
    quint64 peak;
};
struct TextMatch {
    bool valid = false, found = false, wrapped = false;
    quint64 start = 0, end = 0;
    QString error;
};
struct TextReplacement {
    bool valid = false;
    QString text, error;
    quint64 count = 0;
};
struct SqlSelection {
    bool valid;
    quint64 start;
    quint64 end;
    bool confirmation;
};
enum class ObjectInspectionPane { Columns, Indexes, Keys, Ddl };
enum class MetadataAvailability { Available, Unsupported, Unavailable };
struct ObjectProperty {
    QString name, value;
    MetadataAvailability availability = MetadataAvailability::Available;
    QString reason;
};
struct ObjectInspectionRow {
    QString id, name, kind;
    QList<ObjectProperty> properties;
};
struct ObjectInspection {
    ObjectInspectionPane pane = ObjectInspectionPane::Columns;
    MetadataAvailability availability = MetadataAvailability::Available;
    QString reason, ddl;
    QList<ObjectInspectionRow> rows;
};
class EngineAdapter final : public QObject {
    Q_OBJECT
  public:
    explicit EngineAdapter(QObject* parent = nullptr, const QString& storagePath = {});
    ~EngineAdapter() override;
    std::optional<quint64> connectSqlite(const QString& path, bool readOnly = false);
    std::optional<quint64> execute(quint64 connection, const QString& sql, bool autoCommit = true,
                                   const QString& profileId = {},
                                   const QueryPreferences& preferences = {});
    static RecoveryLimits recoveryLimits();
    static QueryPreferenceLimits queryPreferenceLimits();
    bool getQueryPreferences(quint64 token);
    bool setQueryPreferences(const QueryPreferences& preferences, quint64 token);
    static EditorPreferenceLimits editorPreferenceLimits();
    bool getEditorPreferences(quint64 token);
    bool setEditorPreferences(const EditorPreferences& preferences, quint64 token);
    bool getAppearanceLayout(quint64 token);
    bool setAppearanceLayout(const AppearanceLayout& appearance, quint64 token);
    bool resetAppearanceLayout(quint64 token);
    static QStringList keywordCompletions(const QString& prefix);
    static TextMatch findText(const QString& source, const QString& needle, quint64 start,
                              bool backwards, bool caseSensitive, bool wholeWord);
    static TextReplacement replaceAllText(const QString& source, const QString& needle,
                                          const QString& replacement, bool caseSensitive,
                                          bool wholeWord);
    bool listHistory(quint32 limit, quint32 offset, quint64 token);
    bool clearHistory(quint64 token);
    bool getHistoryPolicy(quint64 token);
    bool setHistoryPolicy(const HistoryPolicy& policy, quint64 token);
    bool restoreWorkspace(quint64 token);
    bool saveWorkspace(const QList<SavedEditorDocument>& documents, quint64 token);
    bool saveWorkspaceTabs(const QList<SavedWorkspaceTab>& tabs, quint32 activeIndex,
                           quint64 token);
    bool restoreWorkspaceTabs(quint64 token);
    void listProfiles(quint64 token);
    void saveProfile(const SavedProfile& profile, quint64 token);
    void saveProfileWithPassword(const SavedProfile& profile, const QString& password,
                                 const QString& action, quint64 token);
    void testProfileWithPassword(const SavedProfile& profile, const QString& password,
                                 bool hasPassword, quint64 token);
    std::optional<quint64> connectProfileWithPassword(const SavedProfile& profile,
                                                      const QString& password, bool hasPassword);
    void duplicateProfile(const QString& source, const QString& id, const QString& name,
                          quint64 token);
    void deleteProfile(const QString& id, quint64 token);
    void testProfile(const SavedProfile& profile, quint64 token);
    std::optional<quint64> connectProfile(const SavedProfile& profile);
    void fetchPage(quint64 query);
    void fetchPageAt(quint64 query, quint64 index);
    bool cancelQuery(quint64 query);
    std::optional<quint64> startExport(quint64 query, const QString& path, const QString& format,
                                       const QStringList& table = {}, bool postgres = false);
    void cancelExport(quint64 id);
    void loadValueChunk(quint64 query, quint64 handle, quint64 offset, quint32 maxBytes = 65536);
    void loadMetadata(quint64 connection, const QString& parent = {}, quint64 requestToken = 0);
    std::optional<quint64> openObjectData(quint64 connection, const QString& object,
                                          const QueryPreferences& preferences = {});
    bool inspectEditTarget(quint64 connection, const QString& object, quint64 token);
    bool inspectQueryEdit(quint64 connection, const QString& sql, const QStringList& resultColumns,
                          quint64 token);
    bool applyEditBatch(quint64 connection, const std::vector<ReviewedEditStatement>& statements,
                        quint64 token);
    void loadObjectInspection(quint64 connection, const QString& object, ObjectInspectionPane pane,
                              quint64 requestToken);
    void objectDdl(quint64 connection, const QString& object);
    void commitTransaction(quint64 connection);
    void rollbackTransaction(quint64 connection);
    bool disconnectConnection(quint64 connection);
    void releaseQuery(quint64 query);
    void beginShutdown();
    void shutdown();
    // Single owner; retain during eventReady and release only after dropping the Qt buffers.
    bool retainTransfer(quint64 lease, quint64 payloadBytes);
    void releasePageLease(quint64 lease);
    PageMemoryUsage memoryUsage() const;
    PageCacheUsage cacheUsage() const;
    static SqlSelection executionRange(const QString& sql, quint64 cursor, quint64 start,
                                       quint64 end);
  signals:
    void queryPreferencesReady(quint64 token, const choscordb::QueryPreferences& preferences);
    void shutdownReady();
    void shutdownFailed(const QString& error, bool retryable);
    void historyWriteFailed(quint64 query, const QString& error);
    void historyListed(quint64 token, const QList<choscordb::SavedHistoryEntry>& entries);
    void historyCleared(quint64 token);
    void editorPreferencesReady(quint64 token, const choscordb::EditorPreferences& preferences);
    void appearanceLayoutReady(quint64 token, bool hasSavedValue,
                               const choscordb::AppearanceLayout& appearance);
    void historyPolicyReady(quint64 token, const choscordb::HistoryPolicy& policy);
    void workspaceRestored(quint64 token, const QList<choscordb::SavedEditorDocument>& documents);
    void workspaceTabsRestored(quint64 token, const QList<choscordb::SavedWorkspaceTab>& tabs,
                               quint32 activeIndex);
    void workspaceSaved(quint64 token);
    void recoveryFailed(quint64 token, const QString& error);
    void profilesReady(quint64 token, const QList<choscordb::SavedProfile>& profiles);
    void profileSaved(quint64 token, const choscordb::SavedProfile& profile,
                      const QString& warning);
    void profileDeleted(quint64 token, const QString& id, const QString& warning);
    void profileFailed(quint64 token, const QString& error);
    void profileTested(quint64 token);
    void profileConnectFailed(const QString& error);
    // Direct connections only: the DTO reference lives for the current event dispatch.
    void eventReady(const choscordb::BridgeEvent& event);
    void commandFailed(const QString& error);
    void exportSubmissionFailed(quint64 query, const QString& error);
    void valueChunkSubmissionFailed(quint64 query, quint64 handle, quint64 offset,
                                    const QString& error);
    void objectInspectionReady(quint64 connection, const QString& object, quint64 token,
                               const choscordb::ObjectInspection& inspection);
    void objectInspectionFailed(quint64 connection, const QString& object, quint64 token,
                                const QString& error);
    void metadataSubmissionFailed(quint64 connection, const QString& parent, quint64 token,
                                  const QString& error);

  private:
    bool queueRecovery(quint64 token, std::function<Submit()> command, quint64 bytes = 0,
                       bool duringShutdown = false);
    void pumpRecovery();
    void finishShutdown();
    quint32 pageSizeForQuery(quint64 query) const;
    struct Private;
    std::unique_ptr<Private> d_;
};
} // namespace choscordb

Q_DECLARE_METATYPE(choscordb::SavedProfile)

Q_DECLARE_METATYPE(choscordb::SavedEditorDocument)
Q_DECLARE_METATYPE(choscordb::SavedWorkspaceTab)

Q_DECLARE_METATYPE(choscordb::SavedHistoryEntry)
Q_DECLARE_METATYPE(choscordb::HistoryPolicy)

Q_DECLARE_METATYPE(choscordb::EditorPreferences)

Q_DECLARE_METATYPE(choscordb::QueryPreferences)
Q_DECLARE_METATYPE(choscordb::AppearanceLayout)

Q_DECLARE_METATYPE(choscordb::ObjectInspection)
