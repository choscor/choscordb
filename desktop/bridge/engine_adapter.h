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
    QString review;
    QString error;
};
struct Submit;
// Secrets as typed in the profile form; Rust decides which apply and how each
// is stored or sent (crates/core/src/profile_draft.rs).
struct ProfileSecretDrafts {
    bool saveCredentials = false;
    QString database;
    bool databaseModified = false;
    QString ssh;
    bool sshModified = false;
    QString tls;
    bool tlsModified = false;
    QString sshPrivateKey;
    bool sshPrivateKeyModified = false;
};
struct ProfileFieldError {
    QString field, message;
};
struct SshHostKeyCandidate {
    QString originalHost, hostname, hostKeyAlias, keyType, publicKey, sha256, opaqueJson;
    quint16 port = 22;
};
// Mirrors the bridge ProfileDto. Defaults for new drafts come from Rust
// (EngineAdapter::profileDraftDefaults).
struct SavedProfile {
    QString id, name, groupId, driver, path, host, database, user;
    QString tls, rootCertificate, credentialRef, sshCredentialRef;
    QString tlsClientIdentity, tlsCredentialRef, sshOptions;
    QString proxyOptions, proxyCredentialRef;
    QString sshJumpCredentialRefs;
    QString sshPrivateKeyRef, sshJumpPrivateKeyRefs;
    bool readOnly = false;
    quint16 port = 0;
    bool sshEnabled = false;
    QString sshHost, sshUser, sshAuthentication, sshIdentityFile;
    QString sshIdentitySource;
    quint16 sshPort = 0;
};
struct SavedHistoryEntry {
    QString id, profileId, sql;
    qint64 timestamp = 0;
    quint64 durationMs = 0, rowCount = 0;
    QString status;
    bool hasRowCount = false;
};
// Preference records start from Rust's defaults.
struct HistoryPolicy {
    HistoryPolicy();
    bool enabled;
    quint32 maxAgeDays, maxRecords;
};
struct ShortcutOverride {
    QString command, sequence;
};
struct EditorPreferences {
    EditorPreferences();
    quint32 version;
    QString fontFamily;
    quint16 fontSize;
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
    AppearanceLayout();
    quint32 version;
    QString theme, density, accentKind, accent;
    quint32 navigatorWidth, historyHeight;
    quint16 editorResultsSplit;
    bool navigatorVisible, historyVisible;
    qint32 x, y;
    quint32 width, height;
    bool maximized, hasScreenName;
    QString screenName;
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
// How a staged grid cell is represented; Rust owns what each kind permits.
enum class GridCell : uint8_t { Value, Binary, Deferred, FallbackText, Unavailable };
struct GridCellPolicy {
    bool inlineEditable = false, blocksRowDelete = false, blocksRowDuplicate = false,
         duplicateRequiresLoad = false;
};
// What the desktop may offer for a navigator object kind; Rust owns the table.
struct ObjectKindTraits {
    bool opensObjectTab = false, ddl = false, relation = false, container = false, pinnable = false,
         navigationAnchor = false, searchDescends = false, completionCandidate = false;
    // The owning object tab's pane that shows this kind; -1 when none does.
    int detailPane = -1;
    bool connection = false, column = false, diagram = false;
    // The pane an object tab opens on when its object is selected; -1 for the default.
    int initialPane = -1;
    bool repeatsAcrossParents = false;
};
// How the desktop sequences requests for one driver; Rust owns the rules.
struct DriverWorkflow {
    bool inspectAfterResult = false, sqlModeBeforeExecution = false;
};
// Which connection fields a driver uses and what their blank values mean.
struct ProfileDriverForm {
    bool server = false, userOptional = false, databaseSelectsServer = false;
};
// Why an open manual transaction blocks an action; empty when it does not.
struct TransactionGuard {
    QString applyEdits, enableAutoCommit;
};
// Rust's work bounds for one navigator search pass.
struct NavigatorSearchBudget {
    int quickVisits = 0, quickRequests = 0, quickResults = 0, filterVisits = 0, filterRequests = 0;
};
// The search input a Rust search error belongs to.
enum class SearchInput : uint8_t { General, Needle, Replacement };
struct TextMatch {
    bool valid = false, found = false, wrapped = false;
    quint64 start = 0, end = 0;
    QString error;
    SearchInput errorField = SearchInput::General;
};
struct TextReplacement {
    bool valid = false;
    QString text, error;
    SearchInput errorField = SearchInput::General;
    quint64 count = 0;
};
struct TextLimits {
    quint64 maxDocumentBytes, maxSearchPatternBytes;
};
struct SqlSelection {
    bool valid;
    quint64 start;
    quint64 end;
    bool confirmation;
};
// Values match the Rust object-kind detail panes.
enum class ObjectInspectionPane { Columns = 0, Indexes = 1, Keys = 2, Ddl = 3 };
enum class MetadataAvailability { Available, Unsupported, Unavailable };
struct ObjectProperty {
    QString name, value;
    MetadataAvailability availability = MetadataAvailability::Available;
    QString reason;
};
struct ObjectInspectionRow {
    QString id, name, kind;
    bool primaryKey = false;
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
    // Case-insensitive matches in source order, at most `limit`.
    static TextReplacement replaceAllText(const QString& source, const QString& needle,
                                          const QString& replacement, bool caseSensitive,
                                          bool wholeWord);
    // Rust's editable-document and search-text limits, read once per process.
    static const TextLimits& textLimits();
    static bool searchPatternUsable(const QString& needle);
    // Why replacing one match would leave an unsupported document; empty when it fits.
    static QString replacementError(quint64 documentBytes, quint64 removedBytes,
                                    quint64 addedBytes);
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
    void saveProfileDraft(const SavedProfile& profile, const ProfileSecretDrafts& secrets,
                          quint64 token);
    void testProfileDraft(const SavedProfile& profile, const ProfileSecretDrafts& secrets,
                          quint64 token);
    std::optional<quint64> connectProfileDraft(const SavedProfile& profile,
                                               const ProfileSecretDrafts& secrets);
    void inspectSshHostKeys(const SavedProfile& profile, quint64 token);
    void approveSshHostKey(const SshHostKeyCandidate& candidate, const QString& knownHostsPath,
                           quint64 token);
    static SavedProfile profileDraftDefaults(const SavedProfile& profile);
    static SavedProfile normalizeProfileDraft(const SavedProfile& original,
                                              const SavedProfile& edited,
                                              const ProfileSecretDrafts& secrets);
    static ProfileFieldError validateProfileDraft(const SavedProfile& profile,
                                                  const ProfileSecretDrafts& secrets);
    static quint16 profilePortForDriver(quint16 port, const QString& driver);
    static ProfileDriverForm profileDriverForm(const QString& driver);
    static bool profileHasSavedCredentials(const SavedProfile& profile);
    static bool sshKnownHostsPathValid(const QString& path);
    // Why pasted private key text cannot be used; empty when it can.
    static QString sshPrivateKeyTextError(const QString& text);
    // Input hint for secret fields; Rust validates the draft.
    static int profileSecretMaxBytes();
    // Rust names the copy after `name` and assigns its id.
    void duplicateProfile(const QString& source, const QString& name, quint64 token);
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
    // Rust's per-request value window; a request may ask for less, never more.
    static quint32 valueChunkBytes();
    // Why a resident value cannot open in the value detail view; empty when it can.
    static QString valueDetailError(quint64 bytes);
    void loadValueChunk(quint64 query, quint64 handle, quint64 offset,
                        quint32 maxBytes = valueChunkBytes());
    void loadMetadata(quint64 connection, const QString& parent = {}, quint64 requestToken = 0);
    void loadMetadataPage(quint64 connection, const QString& parent, quint64 requestToken,
                          quint64 offset);
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
    // Read once per process, so paint and flags() paths make no bridge calls.
    static const GridCellPolicy& gridCellPolicy(GridCell kind);
    // Why another staged row cannot be added to a page of `rows`; empty when it can.
    static QString gridRowInsertError(qsizetype rows);
    // Cached per kind, so filter and data() paths make one bridge call per kind.
    static ObjectKindTraits objectKindTraits(const QString& kind);
    // Read once per process.
    static const NavigatorSearchBudget& navigatorSearchBudget();
    static DriverWorkflow driverWorkflow(const QString& driver);
    static const TransactionGuard& transactionGuard(bool transactionActive);
    // Whether a child of `parentKind` appears in the sidebar tree (cached per pair).
    static bool sidebarChildVisible(const QString& parentKind, const QString& kind);
    // Why drop or rename is unavailable for this object; empty when it applies.
    static QString objectActionUnavailableReason(bool rename, const QString& driver,
                                                 const QString& kind, const QString& subtype);
    static bool navigatorObjectVisible(const QString& driver, bool showSystemSchemas,
                                       const QString& qualifiedName);
    static bool systemSchemaNode(const QString& kind, const QString& name);
    // Why `table` cannot name a SQL export target; empty when it can.
    static QString sqlExportTableError(const QStringList& table);
    // Whether `driver` hides system schemas under `showSystemSchemas`.
    static bool systemSchemasHidden(const QString& driver, bool showSystemSchemas);
    static bool appearanceThemeValid(const QString& theme);
    // A new identity for an editor document in workspace recovery.
    static QString newDocumentId();
    // The recovery context of an object tab: its saved profile, else its session connection.
    static QString objectTabContext(const QString& profileId, quint64 connection);
    struct ObjectTabContext {
        bool session = false;
        QString profileId;
    };
    static ObjectTabContext parseObjectTabContext(const QString& context);
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
    // Rust queues storage requests and answers them in order.
    bool submitRecovery(quint64 token, const std::function<Submit()>& command,
                        bool duringShutdown = false);
    void finishShutdown();
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
