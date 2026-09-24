#pragma once
#include "bridge/engine_adapter.h"
#include "models/result_table_model.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <functional>
#include <optional>
class QComboBox;
class QAction;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QTableView;
class QWidget;
namespace choscordb {
class SqlEditor;
class ValueDetailDialog;
class ExportDialog;
class ProfileDialog;
class QuerySettingsController;
class ResultFilterBar;
class EngineAdapter;
struct BridgeEvent;
struct SavedProfile;
struct QueryPreferences;
class QueryWorkspace final : public QObject {
    Q_OBJECT
  public:
    struct Widgets {
        QComboBox* connections;
        QComboBox* mode;
        QAction* run;
        QAction* cancel;
        QAction* commit;
        QAction* rollback;
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
    };
    explicit QueryWorkspace(Widgets widgets, QObject* parent = nullptr);
    ~QueryWorkspace() override;
    EngineAdapter* adapter() const;
    QString profileIdForConnection(quint64 connection) const {
        return connectionProfiles_.value(connection);
    }
    QString driverForConnection(quint64 connection) const {
        return connectionDrivers_.value(connection);
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
    void trackQueryPreferencesSave(quint64 token);
    void applyQueryPreferences(const QueryPreferences& preferences);
    QueryPreferences queryPreferences() const;
    void setExternalWork(bool busy);
    void openObjectData(quint64 connection, const QString& object, const QString& label,
                        const QueryPreferences& preferences,
                        const QString& kind = QStringLiteral("table"), bool preserveView = false);
    void invalidateResult();
    bool hasPendingEdits() const { return model_->hasPendingEdits(); }
    bool activeManualTransaction(quint64 connection) const {
        return pendingTransactions_.contains(connection);
    }
    void refreshEditActions() { updateActions(); }
    bool resolvePendingEdits();
    bool navigationAllowed() const { return !workInFlight() && !stopping_; }
  signals:
    void connectionReady(quint64 connection);
    void documentTargetChanged();
    void openQueryRequested(quint64 connection);
    void activityChanged(bool busy);
    void executionStateChanged(const QString& state);
    void transactionStateChanged(quint64 connection, bool active);

  private:
    void execute();
    quint64 executionModeToken_ = 0;
    bool executionModeReady_ = false;
    QPointer<SqlEditor> executionModeEditor_;
    QString executionModeSql_;
    quint64 executionModeCursor_ = 0, executionModeStart_ = 0, executionModeEnd_ = 0;
    std::optional<quint64> executionModeConnection_;
    bool applyStagedEdits();
    void configureEditability();
    void setupResultViewControls();
    void requestResultView(const QList<ResultFilterCondition>& filters, qint32 sortColumn,
                           const QString& sortDirection);
    void submitResultView(const QList<ResultFilterCondition>& filters, qint32 sortColumn,
                          const QString& sortDirection);
    void updateSortIndicator();
    void clearViewState();
    void clearResult();
    void handleEvent(const BridgeEvent& event);
    bool connectionCanDisconnect(quint64 connection) const;
    void updateActions();
    bool connectionAvailable(quint64 connection) const;
    bool queryAvailable() const;
    bool workInFlight() const;
    void message(const QString& text);
    void setExecutionState(const QString& state, const QString& detail = {});
    std::optional<quint64> selectedConnection() const;
    Widgets widgets_;
    QPointer<EngineAdapter> adapter_;
    QuerySettingsController* querySettings_ = nullptr;
    ResultFilterBar* filterBar_ = nullptr;
    ResultTableModel* model_;
    ValueDetailDialog* detail_ = nullptr;
    ExportDialog* export_ = nullptr;
    ProfileDialog* profiles_ = nullptr;
    QString resultOrigin_;
    QString objectKind_, objectId_;
    QString executedSql_, editParameterStyle_;
    QString editQualifiedName_, editReason_;
    std::vector<QString> editColumnNames_;
    std::vector<bool> editKey_, editGenerated_;
    quint64 editTargetToken_ = 0, editApplyToken_ = 0;
    bool editApplying_ = false, editApplied_ = false;
    bool exporting_ = false;
    std::optional<quint64> query_;
    std::optional<quint64> queryConnection_;
    QHash<quint64, QString> pendingConnections_;
    QHash<quint64, QString> connectionProfiles_;
    QHash<quint64, QString> connectionDrivers_;
    QHash<quint64, QString> connectionSqlModes_;
    QHash<quint64, bool> manualModes_;
    QSet<quint64> pendingTransactions_;
    QSet<quint64> confirmingDisconnects_;
    QSet<quint64> disconnecting_;
    std::vector<ResultColumn> columns_;
    std::optional<quint64> currentPage_;
    std::optional<quint64> visibleLease_;
    std::optional<quint64> schemaLease_;
    bool executionFinished_ = true;
    bool busy_ = false;
    bool fetching_ = false;
    bool hasMore_ = false;
    bool hasMoreResults_ = false;
    bool stopping_ = false;
    bool externalWork_ = false, invalidatePending_ = false;
    bool cancellationPending_ = false;
    QList<ResultFilterCondition> viewFilters_, proposedViewFilters_, deferredViewFilters_;
    qint32 viewSortColumn_ = -1, proposedViewSortColumn_ = -1, deferredViewSortColumn_ = -1;
    QString viewSortDirection_, proposedViewSortDirection_, deferredViewSortDirection_;
    bool viewBusy_ = false, deferredViewRequest_ = false, preserveViewOnRefresh_ = false;
    bool proposedFiltersFromDraft_ = false;
    std::optional<quint64> viewRefreshQuery_;
};
} // namespace choscordb
