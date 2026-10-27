#pragma once
#include "app/main_window.h"
#include <QList>

class QAction;
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
class SidebarSection;
namespace main_window_detail {
class WorkspaceTabs;
}
namespace design {
class Button;
class StatusLine;
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
    QStackedWidget* sidebarPanels{};
    QListWidget* savedConnections{};
    SidebarSection* pinnedSection{};
    QTreeView* pinnedList{};
    design::Text* pinnedEmpty{};
    QLineEdit* filter{};
    QTreeView* tree{};
    design::Text* objectsEmpty{};
    SidebarSection* objectSection{};
    QLineEdit* savedSearch{};
    SidebarSection* savedSection{};
    design::Text* savedStatus{};
    QTreeWidget* savedFiles{};
    QLineEdit* historySearch{};
    SidebarSection* historySection{};
    design::Text* historyStatus{};
    QListWidget* historyItems{};
    QAction* resetLayout{};
    QToolBar* toolbar{};
    QAction* run{};
    QAction* runAll{};
    QAction* cancel{};
    design::Button* cancelButton{};
    QAction* querySettings{};
    QSplitter* splitter{};
    main_window_detail::WorkspaceTabs* workspaceTabs{};
    QWidget* toolbarHost{};
    QStackedWidget* results{};
    design::Text* empty{};
    QTableView* grid{};
    design::StatusLine* resultFooter{};
    design::Text* compactState{};
    design::Text* durationMetric{};
    design::Text* pageMetric{};
    design::Text* rowsMetric{};
    design::Text* visibleSizeMetric{};
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
