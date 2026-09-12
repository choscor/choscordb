#pragma once
#include <QMainWindow>
class QTabWidget;
namespace choscordb {
class SqlEditor;
class QueryWorkspace;
class WorkspaceRecoveryController;
class HistoryDock;
class SearchPanel;
class EditorCompletionController;
class EditorPreferencesController;
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
    bool databaseClosePending_ = false, databaseCloseApproved_ = false;
    HistoryDock* history_ = nullptr;
    EditorPreferencesController* preferences_ = nullptr;
    EditorCompletionController* completion_ = nullptr;
    SearchPanel* search_ = nullptr;
    QTabWidget* editors_;
    QueryWorkspace* workspace_ = nullptr;
};
} // namespace choscordb
