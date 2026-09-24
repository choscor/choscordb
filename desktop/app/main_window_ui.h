#pragma once
#include "app/main_window.h"
#include <QList>

class QAction;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QSplitter;
class QStackedWidget;
class QTableView;
class QToolBar;
class QTreeView;
class QTreeWidget;
class QWidget;
namespace choscordb {
namespace main_window_detail {
class WorkspaceTabs;
}
namespace design {
class Button;
class Text;
} // namespace design
// Construction-only widget references; callbacks capture individual pointers by value.
struct MainWindow::Ui {
    QMenu* fileMenu{};
    QAction* newQuery{};
    QAction* open{};
    QAction* save{};
    QAction* saveAs{};
    QAction* quit{};
    QMenu* editMenu{};
    QMenu* queryMenu{};
    QAction* newConnection{};
    QMenu* viewMenu{};
    QDockWidget* navigator{};
    design::Button* addConnection{};
    design::Button* refreshNavigator{};
    design::Button* disconnectNavigator{};
    QStackedWidget* sidebarPanels{};
    QListWidget* savedConnections{};
    QLineEdit* filter{};
    QTreeView* tree{};
    design::Text* objectsEmpty{};
    QLabel* navigatorStatus{};
    QLineEdit* savedSearch{};
    design::Text* savedStatus{};
    QTreeWidget* savedFiles{};
    QLineEdit* historySearch{};
    design::Text* historyStatus{};
    QListWidget* historyItems{};
    QAction* resetLayout{};
    QToolBar* toolbar{};
    QComboBox* connections{};
    QAction* run{};
    QAction* cancel{};
    design::Button* cancelButton{};
    QComboBox* mode{};
    QAction* commitAction{};
    QAction* rollbackAction{};
    QAction* querySettings{};
    QSplitter* splitter{};
    main_window_detail::WorkspaceTabs* workspaceTabs{};
    QWidget* toolbarHost{};
    QStackedWidget* results{};
    design::Text* empty{};
    QTableView* grid{};
    design::Text* compactState{};
    design::Button* previousPage{};
    design::Button* nextPage{};
    design::Button* exportResult{};
    design::Button* addResultRow{};
    design::Button* deleteResultRows{};
    design::Button* restoreResultRows{};
    design::Button* nullResultCell{};
    design::Button* discardResultEdits{};
    design::Button* applyResultEdits{};
    QPlainTextEdit* messages{};
    QList<QAction*> searchActions{};
    ToastRegion* toast{};
};
} // namespace choscordb
