#pragma once
#include <QMainWindow>
class QTabWidget;
namespace choscordb {
namespace design {
class PlatformAccessibilityMonitor;
class ThemeManager;
} // namespace design
class SqlEditor;
class QueryWorkspace;
class WorkspaceRecoveryController;
class HistoryDock;
class SearchPanel;
class EditorCompletionController;
class EditorPreferencesController;
class AppearanceController;
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget* parent = nullptr, const QString& storagePath = {});

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    SqlEditor* addEditor();
    WorkspaceRecoveryController* recovery_ = nullptr;
    bool recoveryCloseApproved_ = false;
    bool appearanceCloseApproved_ = false;
    bool databaseClosePending_ = false, databaseCloseApproved_ = false;
    HistoryDock* history_ = nullptr;
    EditorPreferencesController* preferences_ = nullptr;
    AppearanceController* appearance_ = nullptr;
    EditorCompletionController* completion_ = nullptr;
    SearchPanel* search_ = nullptr;
    QTabWidget* editors_;
    design::PlatformAccessibilityMonitor* platformAccessibility_ = nullptr;
    design::ThemeManager* theme_ = nullptr;
    QueryWorkspace* workspace_ = nullptr;
};
} // namespace choscordb
