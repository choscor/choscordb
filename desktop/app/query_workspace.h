#pragma once
#include "bridge/engine_adapter.h"
#include "models/result_table_model.h"
#include "models/shortcut_catalog.h"
#include <QHash>
#include <QObject>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
class QAction;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QMenu;
class QTableView;
class QWidget;
namespace choscordb::design {
class RightSheet;
}
namespace choscordb {
class SqlEditor;
class ValueDetailDialog;
class ExportDialog;
class ProfileDialog;
class QuerySettingsController;
class ResultFilterBar;
class EngineAdapter;
class DeferredAssemblerJob;
struct BridgeEvent;
struct SavedProfile;
struct QueryPreferences;
class QueryWorkspace final : public QObject {
    Q_OBJECT
  public:
    struct Widgets {
        QAction* run;
        QAction* runAll;
        QAction* cancel;
        QAction* newConnection;
        QPushButton* nextPage;
        QLabel* summary;
        QPlainTextEdit* messages;
        QTableView* grid;
        std::function<SqlEditor*()> currentEditor;
        QWidget* dialogParent;
        QPushButton* previousPage = nullptr;
        QPushButton* exportResult = nullptr;
        QString storagePath;
        EngineAdapter* sharedAdapter = nullptr;
        bool objectReadOnly = false;
        QPushButton* addRow = nullptr;
        QPushButton* deleteRows = nullptr;
        QPushButton* setNull = nullptr;
        QPushButton* applyEdits = nullptr;
        QPushButton* discardEdits = nullptr;
        std::function<bool(quint64)> transactionActive = {};
        QPushButton* restoreRows = nullptr;
        QLabel* outcome = nullptr;
        QLabel* durationMetric = nullptr;
        QLabel* pageMetric = nullptr;
        QLabel* rowsMetric = nullptr;
        QLabel* visibleSizeMetric = nullptr;
    };
    // A live database session that a SQL document can target.
    struct LiveConnection {
        quint64 id = 0;
        QString label;
    };
    // The staged-edit commands of the result grid, in catalog order.
    enum class GridCommand {
        AddRow,
        DuplicateRow,
        DeleteRows,
        RestoreRows,
        SetNull,
        EditCell,
        ApplyEdits,
        DiscardEdits
    };
    // The catalog rows of the grid commands and of the fixed grid keys.
    static QList<ShortcutDescriptor> gridShortcuts();
    explicit QueryWorkspace(Widgets widgets, QObject* parent = nullptr);
    ~QueryWorkspace() override;
    EngineAdapter* adapter() const;
    QString profileIdForConnection(quint64 connection) const {
        return connectionProfiles_.value(connection);
    }
    QString driverForConnection(quint64 connection) const {
        return connectionDrivers_.value(connection);
    }
    void setDriverForConnection(quint64 connection, const QString& driver) {
        connectionDrivers_.insert(connection, driver);
    }
    bool confirmShutdown();
    void beginShutdown();
    void cancelShutdown();
    void shutdown();
    void connectSqlite(const QString& path);
    std::optional<quint64> connectSavedProfile(const SavedProfile& profile);
    void showProfiles(const QString& profileId = {});
    void manageSavedProfile(const QString& profileId, const QString& action);
    void disconnectConnection(quint64 connection);
    void showQuerySettings();
    void documentChanged();
    bool selectedTargetAvailable() const {
        const auto selected = selectedConnection();
        return selected && connectionAvailable(*selected);
    }
    void trackQueryPreferencesSave(quint64 token);
    void applyQueryPreferences(const QueryPreferences& preferences);
    QueryPreferences queryPreferences() const;
    void setExternalWork(bool busy);
    void setExternalWork(QObject* source, bool busy);
    // False when the read did not start, such as when pending edits were kept.
    bool openObjectData(quint64 connection, const QString& object, const QString& label,
                        const QueryPreferences& preferences, bool preserveView = false,
                        const QString& initialFilter = {}, const ObjectDataTime& time = {});
    void invalidateResult(bool connectionLost = false);
    bool hasPendingEdits() const { return model_->hasPendingEdits(); }
    bool activeTransaction(quint64 connection) const {
        return pendingTransactions_.contains(connection);
    }
    const QList<LiveConnection>& liveConnections() const { return liveConnections_; }
    std::optional<QString> liveConnectionLabel(quint64 connection) const;
    std::optional<LiveConnection> liveConnectionForProfile(const QString& profileId) const;
    // Binds a SQL document to a live connection. Work in flight blocks the change.
    bool changeDocumentConnection(SqlEditor* editor, quint64 connection);
    void refreshEditActions() { updateActions(); }
    bool resolvePendingEdits();
    bool navigationAllowed() const { return !workInFlight() && !stopping_; }
    // Sets the primary key of a grid command. A fixed alternate key stays.
    void setGridShortcut(const QString& id, const QKeySequence& primary);
  signals:
    void connectionReady(quint64 connection);
    void connectionNotice(const QString& notice);
    void documentTargetChanged();
    void openQueryRequested(quint64 connection);
    void activityChanged(bool busy);
    void gridEditPlanningChanged(bool planning);
    void executionStateChanged(const QString& state);
    void transactionStateChanged(quint64 connection, bool active);
    void foreignKeyRequested(quint64 connection, const QString& object, const QString& label,
                             const QString& filter);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    struct ExecutionMetrics {
        QString duration;
        QString page;
        QString rows;
        QString visibleSize;
    };
    void execute(RunScope scope);
    RunScope executionModeScope_ = RunScope::Current;
    quint64 executionModeToken_ = 0;
    bool executionModeReady_ = false;
    QPointer<SqlEditor> executionModeEditor_;
    QString executionModeSql_;
    quint64 executionModeCursor_ = 0, executionModeStart_ = 0, executionModeEnd_ = 0;
    std::optional<quint64> executionModeConnection_;
    bool applyStagedEdits();
    void setupGridCommands();
    void runGridCommand(GridCommand command);
    void updateGridActions();
    void updateApplyToolTip();
    QAction* gridAction(GridCommand command) const;
    // Why a disabled command cannot run, or empty when it has nothing to act on yet.
    QString gridBlockedReason(GridCommand command) const;
    std::optional<GridCommand> gridCommandForKey(QKeyCombination key) const;
    // Stages the open inline editor's value and closes it. False, with the editor
    // still open, when the value did not stage.
    bool commitInlineEditor(QWidget* editor);
    void duplicateRow(const QModelIndex& index);
    void discardStagedEdits();
    // The cell under the pointer while the context menu is open; else the current cell.
    QModelIndex gridTarget() const;
    std::array<QAction*, 8> gridActions_{};
    std::optional<QPersistentModelIndex> menuTarget_;
    void configureEditability();
    void requestCellMetadata();
    void activateForeignKey(const QModelIndex& index);
    void setupResultViewControls();
    bool quickFilterAvailable(const QPersistentModelIndex& clicked) const;
    void appendQuickFilterActions(QMenu& menu, const QPersistentModelIndex& clicked);
    void activateQuickFilter(const QPersistentModelIndex& clicked, quint64 query,
                             CellFilterOperator operation);
    bool quickFilterPreparing_ = false;
    void requestResultView(const QList<ResultFilterCondition>& filters, qint32 sortColumn,
                           const QString& sortDirection);
    void submitResultView(const QList<ResultFilterCondition>& filters, qint32 sortColumn,
                          const QString& sortDirection);
    void updateSortIndicator();
    void clearViewState();
    void clearResult();
    enum class JsonViewMode { Cell, Row, Table };
    void openJsonView(JsonViewMode mode, int row = 0, int column = 0);
    void appendJsonViewActions(QMenu& menu, const QPersistentModelIndex& clicked, bool current);
    bool jsonResultCurrent() const;
    void setupCellEditor();
    bool cellEditEligible(const QPersistentModelIndex& index) const;
    void openCellEditor(const QPersistentModelIndex& index);
    void clearCellEditor();
    void saveCellEditor();
    void cellEditError(const QString& error);
    void scheduleJsonViewEvaluation(bool afterDeferred);
    void requestRowJsonChunk();
    void failRowJson(const QString& error);
    void clearRowJson();
    void handleRowJsonEvent(const BridgeEvent& event);
    void copyResult(int scope);
    void renderCopy(ResultTableModel::CopySnapshot snapshot, QPersistentModelIndex anchor,
                    quint64 query, quint64 selectionGeneration, int scope, quint64 generation);
    void requestCopyChunk();
    void handleCopyEvent(const BridgeEvent& event);
    void failCopy(const QString& error);
    void handleEvent(const BridgeEvent& event);
    // Swaps in a page that a worker built from `page`.
    void showPage(const BridgeEvent& page, ResultTableModel::PreparedPage prepared);
    // A cancellation, a cleared result or a newer page supersedes the page in its build.
    void dropPageBuild();
    bool connectionCanDisconnect(quint64 connection) const;
    void updateActions();
    void updateSetNullAction();
    bool connectionAvailable(quint64 connection) const;
    bool queryAvailable() const;
    bool workInFlight() const;
    // Rust's transaction rules for the connection whose result is being edited.
    const TransactionGuard& editTransactionGuard() const;
    void message(const QString& text);
    void setExecutionState(const QString& state, const QString& detail = {},
                           const ExecutionMetrics& metrics = {});
    void setDocumentStatus(SqlEditor* editor, const QString& state, const QString& detail,
                           const QString& source);
    void presentExecutionState(const QString& state, const QString& detail,
                               const ExecutionMetrics& metrics, const QString& source);
    std::optional<quint64> selectedConnection() const;
    // The status line source of a document without a displayed result.
    QString documentTarget(const SqlEditor* editor) const;
    Widgets widgets_;
    QPointer<EngineAdapter> adapter_;
    QuerySettingsController* querySettings_ = nullptr;
    ResultFilterBar* filterBar_ = nullptr;
    ResultTableModel* model_;
    ValueDetailDialog* detail_ = nullptr;
    QPointer<design::RightSheet> rowJsonSheet_;
    QPointer<QPlainTextEdit> rowJsonText_;
    QPointer<QPushButton> rowJsonCopy_;
    QPointer<QLabel> rowJsonStatus_;
    QPersistentModelIndex rowJsonIndex_;
    quint64 rowJsonGeneration_ = 0;
    bool rowJsonRendering_ = false;
    bool jsonResultInvalidated_ = false;
    QPointer<design::RightSheet> cellEditSheet_;
    QPointer<QPlainTextEdit> cellEditText_;
    QPointer<QPushButton> cellEditSave_;
    QPointer<QLabel> cellEditStatus_;
    QPersistentModelIndex cellEditIndex_;
    quint64 cellResultGeneration_ = 0, cellDraftGeneration_ = 0;
    quint64 cellOpenResultGeneration_ = 0, cellOpenTargetToken_ = 0;
    std::optional<quint64> cellOpenQuery_, cellOpenConnection_;
    bool cellDraftEdited_ = false, cellEditStaging_ = false, cellEditParsing_ = false;
    JsonViewMode rowJsonMode_ = JsonViewMode::Row;
    std::optional<quint64> rowJsonQuery_;
    std::map<std::pair<int, int>, Cell> rowJsonResolved_;
    std::shared_ptr<DeferredAssemblerJob> rowJsonAssembler_;
    bool rowJsonAssemblyPending_ = false;
    int rowJsonLoadingColumn_ = -1;
    int rowJsonLoadingRow_ = -1;
    int rowJsonScanRow_ = -1;
    int rowJsonScanColumn_ = -1;
    quint64 rowJsonLoadingHandle_ = 0;
    quint64 rowJsonLoadingOffset_ = 0;
    quint64 rowJsonResolvedBytes_ = 0;
    struct PendingCopy {
        int scope = 0;
        QItemSelection selection;
        quint64 selectionGeneration = 0;
        QPersistentModelIndex anchor;
        quint64 query = 0;
        ResultTableModel::ResolvedCells resolved;
        std::vector<std::pair<int, int>> deferred;
        std::size_t next = 0;
        std::shared_ptr<DeferredAssemblerJob> assembler;
        bool assembling = false;
        quint64 offset = 0;
        quint64 resolvedBytes = 0;
    };
    std::optional<PendingCopy> pendingCopy_;
    quint64 copyGeneration_ = 0;
    // Changes whenever the grid selection or the rows it addresses change.
    quint64 selectionGeneration_ = 0;
    bool setNullAvailable_ = false;
    bool editabilityReplanQueued_ = false;
    ExportDialog* export_ = nullptr;
    ProfileDialog* profiles_ = nullptr;
    QPointer<SqlEditor> resultEditor_;
    QString resultOrigin_;
    QString commandError_;
    QHash<quint64, QPointer<SqlEditor>> connectionAttemptEditors_;
    QHash<quint64, QVariantMap> connectionAttemptStatus_;
    QString completedAffectedRows_;
    std::optional<quint64> completedDurationMs_;
    QString objectId_;
    ObjectDataTime objectTime_;
    QString executedSql_, editParameterStyle_;
    QString editQualifiedName_, editReason_;
    std::vector<QString> editColumnNames_;
    std::vector<bool> editKey_, editGenerated_, editDuplicable_;
    quint64 editTargetToken_ = 0, editApplyToken_ = 0;
    quint64 editPolicyGeneration_ = 0, editabilityJobToken_ = 0;
    bool editabilityPlanning_ = false, editPlanRunning_ = false;
    bool editApplying_ = false, editApplied_ = false;
    bool exporting_ = false;
    std::optional<quint64> query_;
    std::optional<quint64> queryConnection_;
    QHash<quint64, QString> pendingConnections_;
    QList<LiveConnection> liveConnections_;
    QHash<quint64, QString> connectionProfiles_;
    QHash<quint64, QString> connectionDrivers_;
    QHash<quint64, QString> connectionSqlModes_;
    QSet<quint64> pendingTransactions_;
    QSet<quint64> confirmingDisconnects_;
    QSet<quint64> disconnecting_;
    std::vector<ResultColumn> columns_;
    std::vector<ResultCellMetadata> cellMetadata_;
    quint64 cellMetadataToken_ = 0;
    std::optional<quint64> cellMetadataQuery_;
    QString initialFilter_;
    bool initialFilterPending_ = false;
    bool referenceFilterPending_ = false;
    bool referenceFilterFailed_ = false;
    std::optional<quint64> currentPage_;
    std::optional<quint64> visibleLease_;
    std::optional<quint64> schemaLease_;
    // The request token of the page in its build; dropPageBuild replaces it.
    quint64 pageBuild_ = 0;
    std::optional<quint64> buildingLease_;
    bool executionFinished_ = true;
    bool busy_ = false;
    bool fetching_ = false;
    bool hasMore_ = false;
    bool hasMoreResults_ = false;
    bool stopping_ = false;
    bool externalWork_ = false, invalidatePending_ = false;
    bool invalidateConnectionLost_ = false;
    QHash<QObject*, QMetaObject::Connection> externalWorkSources_;
    bool externalWorkActive() const { return externalWork_ || !externalWorkSources_.isEmpty(); }
    bool cancellationPending_ = false;
    QList<ResultFilterCondition> viewFilters_, proposedViewFilters_, deferredViewFilters_;
    qint32 viewSortColumn_ = -1, proposedViewSortColumn_ = -1, deferredViewSortColumn_ = -1;
    QString viewSortDirection_, proposedViewSortDirection_, deferredViewSortDirection_;
    bool viewBusy_ = false, deferredViewRequest_ = false, preserveViewOnRefresh_ = false;
    bool proposedFiltersFromDraft_ = false;
    std::optional<quint64> viewRefreshQuery_;
};
} // namespace choscordb
