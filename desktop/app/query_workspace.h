#pragma once
#include "models/result_table_model.h"
#include <QHash>
#include <QObject>
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
class EngineAdapter;
struct BridgeEvent;
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
    };
    explicit QueryWorkspace(Widgets widgets, QObject* parent = nullptr);
    ~QueryWorkspace() override;
    EngineAdapter* adapter() const { return adapter_; }
    QString profileIdForConnection(quint64 connection) const {
        return connectionProfiles_.value(connection);
    }
    bool confirmShutdown();
    void beginShutdown();
    void cancelShutdown();
    void shutdown();
    void connectSqlite(const QString& path);
    void disconnectConnection(quint64 connection);
    void showQuerySettings();
  signals:
    void connectionReady(quint64 connection);

  private:
    void execute();
    void clearResult();
    void event(const BridgeEvent& event);
    void updateActions();
    bool connectionAvailable(quint64 connection) const;
    bool queryAvailable() const;
    bool workInFlight() const;
    void message(const QString& text);
    std::optional<quint64> selectedConnection() const;
    Widgets widgets_;
    EngineAdapter* adapter_;
    QuerySettingsController* querySettings_ = nullptr;
    ResultTableModel* model_;
    ValueDetailDialog* detail_ = nullptr;
    ExportDialog* export_ = nullptr;
    ProfileDialog* profiles_ = nullptr;
    bool exporting_ = false;
    std::optional<quint64> query_;
    std::optional<quint64> queryConnection_;
    QHash<quint64, QString> pendingConnections_;
    QHash<quint64, QString> connectionProfiles_;
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
    bool stopping_ = false;
};
} // namespace choscordb
