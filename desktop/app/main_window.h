#pragma once
#include "design_system/toast_region/toast_region.h"
#include <QMainWindow>
#include <QPointer>
#include <QVariant>
#include <functional>
#include <optional>
class QTabWidget;
class QStackedWidget;
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
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    enum class Screen { Start, Sql, Object, History };
    bool showScreen(Screen screen);
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
    void closeEvent(QCloseEvent* event) override;

  private:
    quint64 profileListToken_ = 0;
    std::optional<quint64> pendingBrowseConnection_, browsingConnection_;
    QString pendingBrowseProfileId_, pendingBrowseProfileName_, lastBrowsedProfileId_;
    bool submittingBrowseProfile_ = false;
    NavigatorController* navigatorController_ = nullptr;
    QStackedWidget* screens_ = nullptr;
    ToastRegion* toast_ = nullptr;
    bool constructing_ = true;
    SqlEditor* addEditor();
    ObjectExplorer* makeObjectExplorer();
    void openObjectTab(quint64 connection, const QString& objectId, const QString& label,
                       const QString& kind, const QVariantList& properties, int pane = -1);
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
    design::PlatformAccessibilityMonitor* platformAccessibility_ = nullptr;
    design::ThemeManager* theme_ = nullptr;
    QueryWorkspace* workspace_ = nullptr;
};
} // namespace choscordb
