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
struct RustDiagnostics;
struct ReviewedEditStatement {
    QString sql;
    std::vector<Cell> params;
    // Exact Rust Value kinds accompany the presentation values through review and apply.
    std::vector<QString> paramKinds;
    std::optional<quint64> expectedRows;
};
struct GridEditColumn {
    QString name, resultName, databaseType, enumSourceColumn;
    QStringList enumChoices;
    bool key = false, generated = false;
};
struct GridEditRow {
    std::vector<Cell> current, original;
    std::vector<bool> touched;
    bool inserted = false, deleted = false;
};
struct GridEditRequest {
    QString driver, qualifiedName, parameterStyle, reason;
    bool objectReadOnly = false;
    std::vector<GridEditColumn> columns;
    std::vector<GridEditRow> rows;
};
struct GridEditEligibility {
    std::vector<bool> editable, insertEditable, keyColumns;
    bool canInsert = false, canDelete = false;
    QString reason;
};
struct GridEditPlan {
    std::vector<ReviewedEditStatement> statements;
    QString error;
};
struct Submit;
struct SshHopCredential {
    QString id, secret, action;
    bool hasSecret = false;
    QString privateKey, privateKeyAction;
    bool hasPrivateKey = false;
};
struct SshPrivateKeyCredential {
    QString secret, action;
    bool hasSecret = false;
};
struct SshHostKeyTarget {
    enum class Kind { Target, JumpId, JumpIndex } kind = Kind::Target;
    QString id;
    int index = -1;
};
struct SshHostKeyCandidate {
    SshHostKeyTarget target;
    QString originalHost, hostname, hostKeyAlias, keyType, publicKey, sha256, opaqueJson;
    quint16 port = 22;
};
struct SavedProfile {
    QString id, name, groupId, driver = "sqlite", path, host, database, user;
    QString tls = "prefer", rootCertificate, credentialRef, sshCredentialRef;
    QString tlsClientIdentity, tlsCredentialRef, sshOptions;
    QString proxyOptions, proxyCredentialRef;
    QString sshJumpCredentialRefs;
    QString sshPrivateKeyRef, sshJumpPrivateKeyRefs;
    bool readOnly = false;
    quint16 port = 5432;
    bool sshEnabled = false;
    QString sshHost, sshUser, sshAuthentication = "agent", sshIdentityFile;
    QString sshIdentitySource = "file";
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
    quint32 version, pageSize, timeoutSeconds, connectionTimeoutSeconds;
    bool showSystemSchemas = false;
};
enum class CellFilterOperator : uint8_t {
    Equals,
    NotEquals,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    In,
    Like,
    IsNull,
    IsNotNull
};
struct CellFilterOption {
    CellFilterOperator operation;
    QString label;
    bool enabled;
    QString reason;
};
struct CellFilterComposition {
    QString expression;
    QString validationError;
    QString error;
};
struct ResultFilterCondition {
    quint32 column = 0;
    QString operation;
    QString valueKind;
    QString value;
};
struct QueryPreferenceLimits {
    quint32 version, minPageSize, maxPageSize, defaultPageSize, maxTimeoutSeconds;
    quint32 defaultConnectionTimeoutSeconds, maxConnectionTimeoutSeconds;
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
struct ObjectGraphColumn {
    QString name, databaseType;
    bool primaryKey = false, foreignKey = false;
};
struct ObjectGraphTable {
    QString id, qualifiedName;
    QList<ObjectGraphColumn> columns;
};
struct ObjectGraphEdge {
    QString id, sourceId, targetId;
    QStringList sourceColumns, targetColumns;
};
struct ObjectGraph {
    MetadataAvailability availability = MetadataAvailability::Available;
    QString reason;
    QStringList warnings;
    QList<ObjectGraphTable> tables;
    QList<ObjectGraphEdge> edges;
};
class EngineAdapter final : public QObject {
    Q_OBJECT
  public:
    explicit EngineAdapter(QObject* parent = nullptr, const QString& storagePath = {});
    ~EngineAdapter() override;
    void attachDiagnostics(const RustDiagnostics& service);
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
    bool searchHistory(const QString& query, quint32 limit, quint64 token, quint64 offset = 0);
    bool clearHistory(quint64 token);
    bool historyClearInProgress() const;
    bool getHistoryPolicy(quint64 token);
    bool setHistoryPolicy(const HistoryPolicy& policy, quint64 token);
    bool saveWorkspaceTabs(const QList<SavedWorkspaceTab>& tabs, quint32 activeIndex,
                           quint64 token);
    bool restoreWorkspaceTabs(quint64 token);
    void listProfiles(quint64 token);
    void saveProfile(const SavedProfile& profile, quint64 token);
    void saveProfileWithPassword(const SavedProfile& profile, const QString& password,
                                 const QString& action, quint64 token);
    void saveProfileWithSecrets(const SavedProfile& profile, const QString& databaseSecret,
                                const QString& databaseAction, const QString& sshSecret,
                                const QString& sshAction, quint64 token,
                                const QString& tlsSecret = {}, const QString& tlsAction = "keep",
                                const QString& proxySecret = {},
                                const QString& proxyAction = "keep",
                                const QList<SshHopCredential>& sshHops = {},
                                const SshPrivateKeyCredential& sshPrivateKey = {},
                                bool saveCredentials = true);
    void testProfileWithSecrets(const SavedProfile& profile, const QString& databaseSecret,
                                bool hasDatabaseSecret, const QString& sshSecret, bool hasSshSecret,
                                quint64 token, const QString& tlsSecret = {},
                                bool hasTlsSecret = false, const QString& proxySecret = {},
                                bool hasProxySecret = false,
                                const QList<SshHopCredential>& sshHops = {},
                                const SshPrivateKeyCredential& sshPrivateKey = {});
    void inspectSshHostKeys(const SavedProfile& profile, const SshHostKeyTarget& target,
                            const QList<SshHopCredential>& precedingHopCredentials, quint64 token);
    void approveSshHostKey(const SshHostKeyCandidate& candidate, const QString& knownHostsPath,
                           quint64 token);
    bool validateConnectionProperties(const SavedProfile& profile, QString& error);
    std::optional<quint64> connectProfileWithPassword(const SavedProfile& profile,
                                                      const QString& password, bool hasPassword);
    std::optional<quint64>
    connectProfileWithSecrets(const SavedProfile& profile, const QString& databaseSecret,
                              bool hasDatabaseSecret, const QString& sshSecret, bool hasSshSecret,
                              const QString& tlsSecret = {}, bool hasTlsSecret = false,
                              const QString& proxySecret = {}, bool hasProxySecret = false,
                              const QList<SshHopCredential>& sshHops = {},
                              const SshPrivateKeyCredential& sshPrivateKey = {});
    void duplicateProfile(const QString& source, const QString& id, const QString& name,
                          quint64 token);
    void deleteProfile(const QString& id, quint64 token);
    std::optional<quint64> connectProfile(const SavedProfile& profile);
    bool refreshSqlMode(quint64 connection, quint64 requestToken);
    void nextResultSet(quint64 query);
    void fetchPage(quint64 query);
    void fetchPageAt(quint64 query, quint64 index);
    bool applyResultView(quint64 query, const QList<ResultFilterCondition>& filters,
                         qint32 sortColumn = -1, const QString& sortDirection = {});
    bool cancelResultView(quint64 query);
    bool clearResultView(quint64 query);
    bool cancelQuery(quint64 query);
    std::optional<quint64> startExportDialect(quint64 query, const QString& path,
                                              const QString& format, const QStringList& table,
                                              const QString& dialect);
    void cancelExport(quint64 id);
    void loadValueChunk(quint64 query, quint64 handle, quint64 offset, quint32 maxBytes = 65536);
    void loadMetadata(quint64 connection, const QString& parent = {}, quint64 requestToken = 0);
    void loadMetadataPage(quint64 connection, const QString& parent, quint64 requestToken,
                          quint64 offset, quint32 limit = 1000);
    std::optional<quint64> openObjectData(quint64 connection, const QString& object,
                                          const QueryPreferences& preferences = {});
    bool inspectEditTarget(quint64 connection, const QString& object, quint64 token);
    bool inspectQueryEdit(quint64 connection, const QString& sql, const QStringList& resultColumns,
                          quint64 token);
    bool inspectResultCells(quint64 connection, const QString& object, const QString& sql,
                            const QStringList& resultColumns, quint64 token);
    bool applyEditBatch(quint64 connection, const std::vector<ReviewedEditStatement>& statements,
                        quint64 token);
    static GridEditEligibility gridEditability(const GridEditRequest& request);
    static GridEditPlan planGridEdits(const GridEditRequest& request);
    static QList<CellFilterOption> quickFilterOptions(const QString& column, const Cell& value);
    static CellFilterComposition composeQuickFilter(const QStringList& columns,
                                                    const QString& draft, const QString& column,
                                                    const Cell& value,
                                                    CellFilterOperator operation);
    static bool foreignKeyValueFilterable(const Cell& value);
    static std::optional<QString> foreignKeyPredicate(const QString& targetColumn,
                                                      const Cell& value);
    static std::optional<Cell> parseGridEditValue(const QString& databaseType, const QString& text,
                                                  QString* error = nullptr);
    static bool navigatorObjectVisible(const QString& driver, bool showSystemSchemas,
                                       const QString& qualifiedName);
    static bool postgresSystemSchema(const QString& schema);
    static bool appearanceThemeValid(const QString& theme);
    void loadObjectInspection(quint64 connection, const QString& object, ObjectInspectionPane pane,
                              quint64 requestToken);
    void loadObjectGraph(quint64 connection, const QString& object, quint64 requestToken);
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
                                       quint64 end, const QString& driver = {},
                                       const QString& sqlMode = {});
  signals:
    void queryPreferencesReady(quint64 token, const choscordb::QueryPreferences& preferences);
    void shutdownReady();
    void shutdownFailed(const QString& error, bool retryable);
    void historyWriteFailed(quint64 query, const QString& error);
    void historyListed(quint64 token, const QList<choscordb::SavedHistoryEntry>& entries);
    void historySearched(quint64 token, const QList<choscordb::SavedHistoryEntry>& entries,
                         bool incomplete, quint64 nextOffset);
    void historyClearStarted(quint64 token);
    void historyCleared(quint64 token);
    void editorPreferencesReady(quint64 token, const choscordb::EditorPreferences& preferences);
    void appearanceLayoutReady(quint64 token, bool hasSavedValue,
                               const choscordb::AppearanceLayout& appearance);
    void historyPolicyReady(quint64 token, const choscordb::HistoryPolicy& policy);
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
    void sshHostKeysInspected(quint64 token,
                              const QList<choscordb::SshHostKeyCandidate>& candidates);
    void sshHostKeyApproved(quint64 token, const QString& outcome);
    void sshHostKeyOperationFailed(quint64 token, const QString& error);
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
    void objectGraphReady(quint64 connection, const QString& object, quint64 token,
                          const choscordb::ObjectGraph& graph);
    void objectGraphFailed(quint64 connection, const QString& object, quint64 token,
                           const QString& error);
    void metadataSubmissionFailed(quint64 connection, const QString& parent, quint64 token,
                                  const QString& error);

  private:
    void trackHistoryClears();
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
Q_DECLARE_METATYPE(choscordb::SshHostKeyCandidate)
Q_DECLARE_METATYPE(QList<choscordb::SshHostKeyCandidate>)

Q_DECLARE_METATYPE(choscordb::SavedEditorDocument)
Q_DECLARE_METATYPE(choscordb::SavedWorkspaceTab)

Q_DECLARE_METATYPE(choscordb::SavedHistoryEntry)
Q_DECLARE_METATYPE(choscordb::HistoryPolicy)

Q_DECLARE_METATYPE(choscordb::EditorPreferences)

Q_DECLARE_METATYPE(choscordb::QueryPreferences)
Q_DECLARE_METATYPE(choscordb::AppearanceLayout)

Q_DECLARE_METATYPE(choscordb::ObjectInspection)
Q_DECLARE_METATYPE(choscordb::ObjectGraph)
