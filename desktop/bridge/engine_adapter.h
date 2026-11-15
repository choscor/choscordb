#pragma once
#include "bridge/structure_types.h"
#include "models/result_table_model.h"
#include <QDateTime>
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
enum class CellKind : uint8_t {
    Null,
    Boolean,
    Integer,
    Real,
    Decimal,
    Text,
    Date,
    Time,
    Timestamp,
    Uuid,
    Json,
    Binary,
    FallbackText,
    DeferredFallback,
    Unavailable,
    Deferred,
    InvalidUnicode
};
struct ReviewedEditStatement {
    QString sql;
    std::vector<Cell> params;
    std::vector<CellKind> paramKinds;
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
    std::vector<QString> expressions;
    bool inserted = false, deleted = false;
};
struct GridTemporalText {
    QString text;
    QString error;
};
struct GridPasteCell {
    int row = 0, column = 0;
    QString text;
};
struct GridPastePlan {
    QString error;
    QList<GridPasteCell> cells;
};
struct GridSetValue {
    QString outcome;
    QString text;
    QString expression;
    QString error;
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
    QString environment, environmentLabel, color, safety;
    QString tone, tagLabel;
};
struct SavedHistoryEntry {
    QString id, profileId, sql;
    qint64 timestamp = 0;
    quint64 durationMs = 0, rowCount = 0;
    QString status;
    bool hasRowCount = false;
};
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
    quint8 formatIndent;
    bool formatUppercaseKeywords = true;
};
struct EditorPreferenceLimits {
    quint32 version;
    quint16 defaultFontSize, minFontSize, maxFontSize;
    quint8 defaultFormatIndent, minFormatIndent, maxFormatIndent;
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
struct TableExportFormatChoice {
    QString code, label;
    bool csvOptions = false, sqlOptions = false;
};
struct TableExportObject {
    QString objectId, qualifiedName, kind, relationSubtype;
};
struct TableExportRequest {
    QString driver;
    QList<TableExportObject> objects;
    QString format, delimiter, quote, lineBreak, nullText;
    bool header = true, structure = true, drop = false, data = true, folder = true;
    QString path, pattern;
};
struct TableImportMapping {
    QString target;
    int source = -1;
};
struct TableImportRequest {
    QString driver, path, format, delimiter, quote, nullText, encoding;
    bool header = true, existing = true;
    QString objectId, parentKind, parentQualified, tableName;
    QList<TableImportMapping> mapping;
    QString conflict;
};
struct TableImportPreview {
    bool valid = false;
    QString error;
    QStringList columns;
    int rowCount = 0;
    QStringList cells;
    QList<bool> nulls;
};
struct TableImportReview {
    bool valid = false;
    QString error, sql;
};
struct DatabaseList {
    QList<CodeLabel> choices;
    QString current;
};
struct ProfileDatabase {
    bool valid = false;
    SavedProfile profile;
    QString error;
};
struct ProfileTagOptions {
    QList<CodeLabel> environments, colors, safetyLevels;
    QString labelledEnvironment;
    QString sessionLockedSafety;
    int maxEnvironmentLabelBytes = 0;
};
struct CellFilterComposition {
    QString expression;
    QString validationError;
    QString error;
};
struct ResultSortKey {
    quint32 column = 0;
    QString direction;
    bool operator==(const ResultSortKey&) const = default;
};
struct HiddenColumnsToggle {
    QStringList hidden;
    QString error;
};
struct ResultSortToggle {
    QList<ResultSortKey> sorts;
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
    QString theme;
    quint32 navigatorWidth, historyHeight;
    quint16 editorResultsSplit;
    bool navigatorVisible, historyVisible;
    quint8 sidebarTab;
    QList<QPair<QString, quint32>> resultColumnWidths;
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
struct ObjectDataTime {
    enum class Mode : uint8_t { Current, AsOf, All };
    Mode mode = Mode::Current;
    QString asOf;
};
struct ObjectKindTraits {
    bool opensObjectTab = false, ddl = false, relation = false, container = false, pinnable = false,
         navigationAnchor = false, searchDescends = false, completionCandidate = false;
    int detailPane = -1;
    bool connection = false, column = false, diagram = false;
    int initialPane = -1;
    bool repeatsAcrossParents = false;
    bool objectActions = false;
};
struct DriverWorkflow {
    bool inspectAfterResult = false, sqlModeBeforeExecution = false;
};
struct ProfileDriverForm {
    bool server = false, userOptional = false, databaseSelectsServer = false;
};
struct TransactionGuard {
    QString applyEdits;
};
enum class RowStatusKind : uint8_t { None, Loading, Failed, Unavailable };
enum class RowRetry : uint8_t { None, Refresh };
enum class RowStatusCause : uint8_t {
    Connecting,
    ChildrenLoading,
    ChildrenFailed,
    ConnectionFailed,
    ConnectionUnavailable,
    ConnectionNotReady,
    ConnectionDeselected,
    RevealFailed,
    ObjectUnavailable,
    Connected,
    Hidden,
    Disconnected,
    ChildrenCached,
    ChildrenCachedFailed
};
enum class RecoveryHint : uint8_t {
    Retry,
    RetryQuery,
    RefineOrRetry,
    Refresh,
    ActivatePin,
    RetryFromNavigator,
    RetryFromHistory,
    OpenPreferences,
    RetryOrResetAppearance,
    RetryAppearanceBeforeSave,
    RestoreConnection,
    RefreshNavigator,
    CheckConnection,
    ReconnectAndRefresh,
    RetryRecovery,
    RetryConnection,
    InspectSshServer,
    UseApprovedKey,
    RetryApproval,
    ReconnectManually,
    ReconnectAndRefreshNavigator,
};
struct RowStatus {
    RowStatusKind kind = RowStatusKind::None;
    RowRetry retry = RowRetry::None;
    QString message;
};
enum class Tone : uint8_t { Neutral, Success, Error };
enum class ToastTone : uint8_t { Error, Success, Warning };
enum class ProfileOperation : uint8_t {
    Loading,
    Saving,
    Duplicating,
    Deleting,
    Connecting,
    Listed,
    Saved,
    Deleted,
    Connected,
    Failed,
    ConnectUnsubmitted,
    ConnectionClosed,
    ProfileMissing,
    ListRefreshFailed
};
enum class PanelBody : uint8_t { Content, Empty, NoMatch, FailedPlaceholder, None };
enum class ResultPane : uint8_t { Grid, Messages, Unchanged };
struct Presentation {
    Tone tone = Tone::Neutral;
    bool busy = false;
    PanelBody body = PanelBody::None;
    ResultPane pane = ResultPane::Unchanged;
    RowRetry retry = RowRetry::None;
    QString message;
    std::optional<QString> toast;
    QString toastTitle;
    ToastTone toastTone = ToastTone::Error;
};
template <typename State> struct Transition {
    bool allowed = false;
    State state{};
    QString error;
};
enum class QueryState : uint8_t {
    Idle,
    Connecting,
    Queued,
    Running,
    Cancelling,
    CompletedRows,
    CompletedEmpty,
    Failed,
    Cancelled,
    Disconnected
};
enum class QueryEvent : uint8_t {
    Connect,
    SessionReady,
    ConnectFailed,
    Submit,
    Start,
    Fetch,
    Cancel,
    TimedOut,
    FinishRows,
    FinishEmpty,
    Fail,
    CancelSettled,
    Disconnect,
    Reset
};
enum class PanelKind : uint8_t {
    SavedSql,
    History,
    Pins,
    Objects,
    ObjectDetails,
    Diagram,
    QuickSearch,
    Connections,
    Welcome,
    ObjectSelection
};
enum class PanelState : uint8_t {
    Idle,
    Loading,
    Ready,
    Empty,
    NoMatch,
    Failed,
    Disconnected,
    Incomplete,
    Unsupported
};
enum class PanelWork : uint8_t { None, Loading, Clearing, Saving };
struct PanelFacts {
    bool failed = false;
    PanelWork work = PanelWork::None;
    bool loaded = false;
    bool hasRows = false;
    bool filtered = false;
    bool paged = false;
    bool disconnected = false;
};
struct PanelObservation {
    PanelState state = PanelState::Idle;
    Presentation presentation;
};
enum class PanelEvent : uint8_t {
    Load,
    LoadedRows,
    LoadedNone,
    LoadedPartial,
    LoadUnsupported,
    LoadFailed,
    FilterMatched,
    FilterMissed,
    Disconnect
};
enum class ExportState : uint8_t {
    Idle,
    Submitting,
    Running,
    Cancelling,
    Succeeded,
    Cancelled,
    Failed
};
enum class ExportEvent : uint8_t { Submit, Start, Cancel, Finish, Fail, CancelSettled, Reset };
enum class UpdaterState : uint8_t {
    Idle,
    Checking,
    UpToDate,
    Available,
    Downloading,
    ReadyToInstall,
    Installing,
    Cancelling,
    Cancelled,
    Failed
};
enum class UpdaterEvent : uint8_t {
    Check,
    NoUpdate,
    Found,
    Download,
    Downloaded,
    Install,
    Cancel,
    CancelSettled,
    Fail,
    Dismiss,
    Staged
};
enum class EditPhase : uint8_t { Clean, Pending, Planning, Applying, Failed };
enum class TransactionPhase : uint8_t { Inactive, Active, Ending };
struct GridEditState {
    EditPhase edits = EditPhase::Clean;
    TransactionPhase transaction = TransactionPhase::Inactive;
    bool operator==(const GridEditState&) const = default;
};
enum class GridEditNote : uint8_t { PlanUnfinished, SubmitFailed, WorkspaceClosed, ReadOnly };
enum class SshTrustNote : uint8_t {
    Inspecting,
    SettingsChanged,
    NoHostKeys,
    KeysInspected,
    ApprovalUnknown,
    ApprovalStale
};
enum class GridEditEvent : uint8_t {
    Edit,
    Plan,
    Planned,
    PlanFailed,
    Apply,
    Applied,
    ApplyFailed,
    Discard,
    Begin,
    Commit,
    Rollback,
    Ended,
    EndFailed,
    Disconnect
};
enum class ProfileTestState : uint8_t {
    Idle,
    Testing,
    Succeeded,
    Failed,
    AwaitingTrust,
    Trusting,
    Trusted
};
enum class ProfileTestEvent : uint8_t {
    Test,
    Passed,
    TestFailed,
    HostKeyUnknown,
    Approve,
    Approved,
    ApprovalFailed,
    Reject,
    Edit
};
struct NavigatorSearchBudget {
    int quickVisits = 0, quickRequests = 0, quickResults = 0, filterVisits = 0, filterRequests = 0;
};
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
enum class RunScope { Current, All };
struct SqlTextEdit {
    bool valid = false;
    quint64 start = 0;
    quint64 end = 0;
    QString text;
    QString error;
};
struct RowLimitedSql {
    QString sql;
    std::optional<quint32> applied;
};
struct SqlSelection {
    bool valid;
    quint64 start;
    quint64 end;
    bool confirmation;
    bool readsOnly = false;
};
struct PlannedStatement {
    quint64 start = 0;
    quint64 end = 0;
    QString label;
};
struct StatementPlan {
    bool valid = false;
    QString error;
    QList<PlannedStatement> statements;
};
enum class SqlGateDecision : uint8_t { Run, Confirm, Reject };
struct SqlGate {
    SqlGateDecision decision = SqlGateDecision::Reject;
    QString title, message, accept;
};
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
    std::optional<quint64> execute(quint64 connection, const QString& sql,
                                   const QString& profileId = {},
                                   const QueryPreferences& preferences = {});
    static QueryPreferenceLimits queryPreferenceLimits();
    bool getQueryPreferences(quint64 token);
    bool setQueryPreferences(const QueryPreferences& preferences, quint64 token);
    static QString resultColumnsContext(const QString& profileId, const QString& objectId);
    static HiddenColumnsToggle toggleHiddenColumn(const QStringList& hidden, const QString& name,
                                                  bool hide, const QStringList& columns);
    bool getHiddenColumns(const QString& context, quint64 token);
    bool setHiddenColumns(const QString& context, const QStringList& hidden, quint64 token);
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
    static const TextLimits& textLimits();
    static bool searchPatternUsable(const QString& needle);
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
    static ProfileDatabase profileWithDatabase(const SavedProfile& profile, const QString& target);
    bool listDatabases(quint64 connection, quint64 token);
    static SavedProfile normalizeProfileDraft(const SavedProfile& original,
                                              const SavedProfile& edited,
                                              const ProfileSecretDrafts& secrets);
    static ProfileFieldError validateProfileDraft(const SavedProfile& profile,
                                                  const ProfileSecretDrafts& secrets);
    static quint16 profilePortForDriver(quint16 port, const QString& driver);
    static ProfileDriverForm profileDriverForm(const QString& driver);
    static bool profileHasSavedCredentials(const SavedProfile& profile);
    static QString sshKnownHostsPathError(const QString& path);
    static QString sshKnownHostsPathRule();
    static QString sshPrivateKeyTextError(const QString& text);
    static int profileSecretMaxBytes();
    void duplicateProfile(const QString& source, const QString& name, quint64 token);
    void deleteProfile(const QString& id, quint64 token);
    std::optional<quint64> connectProfile(const SavedProfile& profile);
    bool refreshSqlMode(quint64 connection, quint64 requestToken);
    void nextResultSet(quint64 query);
    void fetchPage(quint64 query);
    void fetchPageAt(quint64 query, quint64 index);
    bool applyResultView(quint64 query, const QList<ResultFilterCondition>& filters,
                         const QList<ResultSortKey>& sorts = {});
    static ResultSortToggle toggleResultSort(const QList<ResultSortKey>& sorts, quint32 column,
                                             bool additive);
    bool cancelResultView(quint64 query);
    bool clearResultView(quint64 query);
    bool cancelQuery(quint64 query);
    std::optional<quint64> startExportDialect(quint64 query, const QString& path,
                                              const QString& format, const QStringList& table,
                                              const QString& dialect);
    void cancelExport(quint64 id);
    static QList<TableExportFormatChoice> tableExportFormats();
    static QList<CodeLabel> tableExportLineBreaks();
    static TableExportRequest tableExportDefaults();
    static QString tableExportError(const TableExportRequest& request);
    std::optional<quint64> exportTables(quint64 connection, const TableExportRequest& request,
                                        QString* error);
    static QString tableImportUnavailableReason(const QString& kind, const QString& subtype);
    static QList<TableExportFormatChoice> tableImportFormats();
    static QList<CodeLabel> tableImportEncodings();
    static QList<CodeLabel> tableImportConflicts(const QString& driver);
    static TableImportRequest tableImportDefaults();
    static QList<int> tableImportDefaultMapping(const QStringList& tableColumns,
                                                const QStringList& fileColumns);
    static QString tableImportError(const TableImportRequest& request);
    static TableImportPreview tableImportPreview(const TableImportRequest& request);
    static TableImportReview tableImportReview(const TableImportRequest& request,
                                               const QString& qualifiedName,
                                               const QStringList& tableColumns,
                                               const QStringList& keyColumns,
                                               const QStringList& fileColumns);
    std::optional<quint64> importTable(quint64 connection, const TableImportRequest& request,
                                       QString* error);
    static QList<CodeLabel> sqlFileModes();
    std::optional<quint64> runSqlFile(quint64 connection, const QString& path,
                                      const QString& driver, const QString& sqlMode,
                                      const QString& mode, QString* error);
    static QString structureUnavailableReason(const QString& kind, const QString& subtype);
    static QList<StructureChangeKindInfo> structureChangeKinds(const QString& driver,
                                                               bool newTable);
    static QStringList structureTypes(const QString& driver);
    static StructurePlan structurePlan(const StructureTable& table,
                                       const QList<StructureChange>& changes);
    bool applyStructure(quint64 connection, const StructureTable& table,
                        const QList<StructureChange>& changes, quint64 requestToken,
                        QString* error);
    static quint32 valueChunkBytes();
    static QString valueDetailError(quint64 bytes);
    void loadValueChunk(quint64 query, quint64 handle, quint64 offset,
                        quint32 maxBytes = valueChunkBytes());
    void loadMetadata(quint64 connection, const QString& parent = {}, quint64 requestToken = 0,
                      bool fresh = false);
    void loadMetadataPage(quint64 connection, const QString& parent, quint64 requestToken,
                          quint64 offset);
    std::optional<quint64> openObjectData(quint64 connection, const QString& object,
                                          const QueryPreferences& preferences = {},
                                          const ObjectDataTime& time = {});
    bool inspectEditTarget(quint64 connection, const QString& object, quint64 token,
                           const ObjectDataTime& time = {});
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
    static const GridCellPolicy& gridCellPolicy(GridCell kind);
    static QString gridRowInsertError(qsizetype rows);
    static ObjectKindTraits objectKindTraits(const QString& kind);
    static const NavigatorSearchBudget& navigatorSearchBudget();
    static const QList<CodeLabel>& copyFormats();
    static const QList<CodeLabel>& setValueKinds();
    static bool gridBinaryColumn(const QString& databaseType);
    static QString gridTemporalKind(const QString& databaseType);
    static GridTemporalText gridTemporalText(const QString& kind, const QDateTime& picked);
    static GridSetValue gridSetValue(const QString& kind, const QString& driver,
                                     const QString& databaseType, bool inserted,
                                     std::optional<bool> nullable);
    bool explain(quint64 connection, const QString& driver, const QString& statement,
                 quint64 requestToken, QString* error);
    static GridPastePlan gridPastePlan(const QString& text, int top, int left, int rows,
                                       int columns, int gridRows, int gridColumns);
    static const ProfileTagOptions& profileTagOptions();
    static DriverWorkflow driverWorkflow(const QString& driver);
    static const TransactionGuard& transactionGuard(bool transactionActive);
    static RowStatus rowStatus(RowStatusCause cause, const QString& detail = {});
    static const Presentation& queryPresentation(QueryState state);
    static Presentation queryPresentation(QueryState state, const QString& detail);
    static Transition<QueryState> queryTransition(QueryState state, QueryEvent event);
    static QueryState queryResultState(bool hasColumns, quint64 rows);
    static const Presentation& panelPresentation(PanelKind kind, PanelState state);
    static Presentation panelPresentation(PanelKind kind, PanelState state, const QString& detail);
    static Transition<PanelState> panelTransition(PanelState state, PanelEvent event);
    static PanelObservation panelObservation(PanelKind kind, const PanelFacts& facts,
                                             const QString& error = {});
    static Presentation exportPresentation(ExportState state, const QString& detail = {});
    static Presentation importPresentation(ExportState state, const QString& detail = {});
    static Presentation sqlFilePresentation(ExportState state, const QString& detail = {});
    static Transition<ExportState> exportTransition(ExportState state, ExportEvent event);
    static Presentation updaterPresentation(UpdaterState state, const QString& detail = {});
    static Transition<UpdaterState> updaterTransition(UpdaterState state, UpdaterEvent event);
    static Presentation gridEditPresentation(GridEditState state, const QString& detail = {});
    static Transition<GridEditState> gridEditTransition(GridEditState state, GridEditEvent event);
    static Presentation profileTestPresentation(ProfileTestState state, const QString& detail = {});
    static Transition<ProfileTestState> profileTestTransition(ProfileTestState state,
                                                              ProfileTestEvent event);
    static const QString& recoveryHint(RecoveryHint hint);
    static QString gridEditNote(GridEditNote note);
    static QString sshTrustNote(SshTrustNote note);
    static Presentation profileOperationPresentation(ProfileOperation operation,
                                                     const QString& detail = {});
    static int panelNoticeMs();
    static int filterDelayMs();
    static int layoutSaveDelayMs();
    static int tabCountSampleMs();
    static int pinRevealRetryMs();
    static int recoverySaveDelayMs();
    static int completionDelayMs();
    static int jsonMenuRetryMs();
    static int trustValidityPollMs();
    static int signalCoalesceMs();
    static int startupPollMs();
    static quint32 historyPageRows();
    static quint32 recentHistoryRows();
    static bool sidebarChildVisible(const QString& parentKind, const QString& kind);
    static bool relationSubtypeHasHistory(const QString& subtype);
    static bool navigatorObjectVisible(const QString& driver, bool showSystemSchemas,
                                       const QString& qualifiedName);
    static bool systemSchemaNode(const QString& kind, const QString& name);
    static QString sqlExportTableError(const QStringList& table);
    static bool systemSchemasHidden(const QString& driver, bool showSystemSchemas);
    static bool appearanceThemeValid(const QString& theme);
    static QString newDocumentId();
    static QString objectTabContext(const QString& profileId, quint64 connection);
    struct ObjectTabContext {
        bool session = false;
        QString profileId;
    };
    static ObjectTabContext parseObjectTabContext(const QString& context);
    void loadObjectInspection(quint64 connection, const QString& object, ObjectInspectionPane pane,
                              quint64 requestToken);
    void loadObjectGraph(quint64 connection, const QString& object, quint64 requestToken);
    bool objectDdl(quint64 connection, const QString& object, quint64 requestToken);
    bool disconnectConnection(quint64 connection);
    void releaseQuery(quint64 query);
    void beginShutdown();
    void shutdown();
    bool retainTransfer(quint64 lease, quint64 payloadBytes);
    std::shared_ptr<const BridgeEvent> retainEvent(const BridgeEvent& event);
    bool shrinkPageLease(quint64 lease, quint64 payloadBytes);
    void releasePageLease(quint64 lease);
    PageMemoryUsage memoryUsage() const;
    PageCacheUsage cacheUsage() const;
    static SqlTextEdit formatSql(const QString& sql, quint64 cursor, quint64 start, quint64 end,
                                 const QString& driver, const QString& sqlMode, quint8 indent,
                                 bool uppercaseKeywords, bool minify);
    static RowLimitedSql applyRowLimit(const QString& sql, quint32 limit, const QString& driver,
                                       const QString& sqlMode);
    static const QList<quint32>& rowLimitChoices();
    static SqlTextEdit toggleSqlComment(const QString& sql, quint64 start, quint64 end, bool block);
    static SqlSelection executionRange(const QString& sql, quint64 cursor, quint64 start,
                                       quint64 end, RunScope scope, const QString& driver = {},
                                       const QString& sqlMode = {});
    static StatementPlan statementPlan(const QString& sql, quint64 start, quint64 end,
                                       const QString& driver, const QString& sqlMode);
    static SqlGate executionGate(const QString& safety, const SqlSelection& range);
  signals:
    void queryPreferencesReady(quint64 token, const choscordb::QueryPreferences& preferences);
    void hiddenColumnsReady(quint64 token, const QString& context, const QStringList& hidden);
    void databasesReady(quint64 connection, quint64 token, const choscordb::DatabaseList& list);
    void databasesFailed(quint64 connection, quint64 token, const QString& error);
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
    void requestMetadataPage(quint64 connection, const QString& parent, quint64 requestToken,
                             quint64 offset, bool fresh);
    void trackHistoryClears();
    bool submitRecovery(quint64 token, const std::function<Submit()>& command,
                        bool duringShutdown = false);
    void finishShutdown();
    struct Private;
    std::unique_ptr<Private> d_;
};
}

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
