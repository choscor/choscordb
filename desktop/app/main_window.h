#pragma once
#include "design_system/toast_region/toast_region.h"
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QVariant>
#include <functional>
#include <limits>
#include <optional>
class QTabWidget;
class QStackedWidget;
class QListWidget;
namespace choscordb {
namespace design {
class PlatformAccessibilityMonitor;
class ThemeManager;
} // namespace design
class SqlEditor;
class QueryWorkspace;
class ObjectExplorer;
class NavigatorController;
class WorkspaceRecoveryController;
class HistoryDock;
class SearchPanel;
class EditorCompletionController;
class EditorPreferencesController;
class AppearanceController;
struct BridgeEvent;
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    enum class Screen { Start, Sql, Object, History };
    bool showScreen(Screen screen);
    void requestUpdateRestart(std::function<void()> install);
    void openConnectionQuery(quint64 connection);
    void showToast(const QString& message, ToastVariant variant);
    std::optional<quint64> browsingConnection() const { return browsingConnection_; }
  signals:
    void browsingConnectionChanged(quint64 connection);
    void objectContextSelected(quint64 connection, const QString& objectId,
                               const QString& qualifiedName, const QString& kind);

  public:
    explicit MainWindow(QWidget* parent = nullptr, const QString& storagePath = {});

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
#ifdef Q_OS_MACOS
    void showEvent(QShowEvent* event) override;
#endif

  private:
    struct Ui;
    Ui buildUi();
    void refreshResultFooterColor();
    void connectWorkspace(const Ui& ui, const QString& storagePath);
    void connectLifecycle(const Ui& ui, const QString& storagePath);
    void connectNavigator(const Ui& ui);
    void requestObjectAction(const QString& action, quint64 connection, const QString& objectId,
                             const QString& shortName, const QString& kind,
                             const QString& parentObjectId, const QString& qualifiedName,
                             const QString& relationSubtype);
    void handleObjectActionEvent(const BridgeEvent& event);
    bool objectActionReady(quint64 connection, const QString& objectId, const QString& kind,
                           const QString& parentObjectId, const QString& qualifiedName,
                           const QString& relationSubtype);
#ifdef Q_OS_MACOS
    void updateNativeTitleBar();
#endif
    quint64 profileListToken_ = 0;
    struct PendingBrowse {
        quint64 placeholder = 0;
        std::optional<quint64> connection;
        QString name;
    };
    std::optional<quint64> browsingConnection_;
    QSet<QString> selectedProfileIds_;
    QHash<QString, PendingBrowse> pendingBrowseProfiles_;
    QHash<quint64, QString> sessionProfileIds_;
    QHash<QString, quint64> selectedSessionIds_;
    QSet<quint64> retiredBrowseConnections_;
    quint64 nextPendingPlaceholder_ = std::numeric_limits<quint64>::max();
    QString submittingBrowseProfileId_, submissionError_;
    bool submittingBrowseProfile_ = false;
    NavigatorController* navigatorController_ = nullptr;
    struct PendingObjectAction {
        QString action, objectId, kind, displayKind, parentObjectId, qualifiedName;
        QString newObjectId, newQualifiedName, context;
        quint64 connection = 0, query = 0;
    };
    std::optional<PendingObjectAction> pendingObjectAction_;
    struct PendingObjectRefresh {
        quint64 connection = 0, token = 0;
        QString parentObjectId;
    };
    std::optional<PendingObjectRefresh> pendingObjectRefresh_;
    QStackedWidget* screens_ = nullptr;
    ToastRegion* toast_ = nullptr;
    bool constructing_ = true;
    std::function<void()> updateInstall_;
    void finishClose(QCloseEvent* event);
    SqlEditor* addEditor();
    ObjectExplorer* makeObjectExplorer();
    void openObjectTab(quint64 connection, const QString& objectId, const QString& label,
                       const QString& kind, const QVariantList& properties, int pane = -1);
    void openReferencedRow(quint64 connection, const QString& objectId, const QString& label,
                           const QString& filter);
    bool allowDocumentChange();
    QPointer<QWidget> activeDocument_;
    QPointer<SqlEditor> lastSqlDocument_;
    QPointer<ObjectExplorer> lastObjectTab_;
    int nextDocumentNumber_ = 0;
    WorkspaceRecoveryController* recovery_ = nullptr;
    bool recoveryCloseApproved_ = false;
    bool appearanceCloseApproved_ = false;
    bool databaseClosePending_ = false, databaseCloseApproved_ = false;
    HistoryDock* history_ = nullptr;
    quint64 sidebarHistoryToken_ = 0;
    bool sidebarHistoryOpen_ = false;
    std::function<void()> refreshSavedFiles_;
    EditorPreferencesController* preferences_ = nullptr;
    AppearanceController* appearance_ = nullptr;
    EditorCompletionController* completion_ = nullptr;
    SearchPanel* search_ = nullptr;
    QTabWidget* editors_ = nullptr;
    QWidget* sqlResultArea_ = nullptr;
    ObjectExplorer* initialObjectExplorer_ = nullptr;
    std::function<void(quint64, const QString&)> openGeneratedSql_;
    std::function<bool(const QString&)> reconnectProfile_;
    QPointer<QListWidget> savedConnectionsList_;
    std::function<void()> activateFocusedSavedProfile_;
    design::PlatformAccessibilityMonitor* platformAccessibility_ = nullptr;
    design::ThemeManager* theme_ = nullptr;
    QueryWorkspace* workspace_ = nullptr;
};
} // namespace choscordb
