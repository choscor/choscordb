#include "navigator_sql_workspace_test.h"
#include "app/appearance_controller.h"
#include "app/main_window.h"
#include "app/navigator_controller.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/menu/embedded_popup.h"
#include "design_system/menu/menu.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "models/result_table_model.h"
#include "widgets/history_dock/history_dock.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest>

void NavigatorSqlWorkspaceTest::sqlRowActionsLiveInResultContextMenu() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QTRY_VERIFY(window.findChild<choscordb::AppearanceController*>()->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* grid = window.findChild<QTableView*>("queryResults");
    QVERIFY(grid);
    auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
    QVERIFY(model);
    const QStringList names = {"queryResultAddRow", "queryResultDeleteRows",
                               "queryResultRestoreRows"};
    const auto invoke = [&](const QString& name, bool enabled) {
        QTimer::singleShot(0, grid, [&] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            QVERIFY(menu);
            menu->close();
            for (const auto& actionName : names) {
                auto* action = menu->findChild<QAction*>(actionName);
                QVERIFY2(action, qPrintable(actionName));
            }
            auto* action = menu->findChild<QAction*>(name);
            QCOMPARE(action->isEnabled(), enabled);
            if (enabled)
                action->trigger();
        });
        grid->customContextMenuRequested(QPoint(10, 10));
    };
    invoke(names[0], false);
    for (const auto& name : names) {
        auto* button = window.findChild<QPushButton*>(name);
        QVERIFY(button);
        QVERIFY(button->isHidden());
        for (auto* toolbar : window.findChildren<QToolBar*>())
            for (auto* action : toolbar->actions())
                QVERIFY(toolbar->widgetForAction(action) != button);
    }
    choscordb::ResultColumn column{};
    column.name = "value";
    QVERIFY(model->setPage({column}, {{QString("first")}, {QString("second")}}, 0));
    model->setEditableColumns({true}, true, true);
    QVERIFY(model->setData(model->index(0, 0), QString("edited")));
    invoke(names[0], true);
    QCOMPARE(model->rowCount(), 3);
    grid->selectionModel()->select(model->index(1, 0), QItemSelectionModel::Select);
    invoke(names[1], true);
    QVERIFY(!model->deleted()[0]);
    QVERIFY(model->deleted()[1]);
    invoke(names[2], true);
    QVERIFY(!model->deleted()[1]);
}

void NavigatorSqlWorkspaceTest::sqlOpensWithEqualEditorAndResults() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QTRY_VERIFY(window.findChild<choscordb::AppearanceController*>()->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* views = window.findChild<QStackedWidget*>("sqlResultViews");
    auto* splitter = qobject_cast<QSplitter*>(views->parentWidget()->parentWidget());
    QVERIFY(splitter);
    QTRY_VERIFY(views->isVisible());
    QCoreApplication::processEvents();
    const auto sizes = splitter->sizes();
    QVERIFY2(qAbs(sizes[0] - sizes[1]) <= 2,
             qPrintable(QString("Editor %1, results %2").arg(sizes[0]).arg(sizes[1])));
}

void NavigatorSqlWorkspaceTest::sqlToolbarInsetsControls() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    QCoreApplication::processEvents();
    auto* toolbar = window.findChild<QWidget*>("queryToolbarContainer");
    auto* selector = window.findChild<QComboBox*>("connectionSelector");
    const auto topLeft = selector->mapTo(toolbar, QPoint());
    QVERIFY(topLeft.x() >= 4);
    QVERIFY(topLeft.y() >= 4);
    QVERIFY(toolbar->height() - topLeft.y() - selector->height() >= 4);
}

void NavigatorSqlWorkspaceTest::tabContextMenuFollowsCursor() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.move(0, 150);
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    QCoreApplication::processEvents();
    auto* bar = window.findChild<QTabWidget*>("editorTabs")->tabBar();
    const auto point = bar->tabRect(0).center();
    const auto cursor = bar->mapToGlobal(point);
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, cursor);
    QApplication::sendEvent(bar, &event);
    auto* menu = window.findChild<QMenu*>("editorTabContextMenu");
    QVERIFY(menu && menu->isVisible());
    const int margin = choscordb::design::detail::menuShadowMargin();
    const auto menuOrigin = menu->mapToGlobal(QPoint());
    QCOMPARE(menuOrigin.x() + margin, cursor.x());
    const int offsetY = menuOrigin.y() + margin - cursor.y();
    QVERIFY(offsetY >= 0 && offsetY <= menu->height());
}

void NavigatorSqlWorkspaceTest::tabContextMenuClosesRequestedDocuments_data() {
    QTest::addColumn<int>("action");
    QTest::addColumn<QStringList>("remaining");
    QTest::newRow("close") << 0 << QStringList{"first", "third", "fourth"};
    QTest::newRow("others") << 1 << QStringList{"second"};
    QTest::newRow("all") << 2 << QStringList{};
    QTest::newRow("right") << 3 << QStringList{"first", "second"};
}

void NavigatorSqlWorkspaceTest::tabContextMenuClosesRequestedDocuments() {
    QFETCH(int, action);
    QFETCH(QStringList, remaining);
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QVERIFY(recovery);
    QTRY_VERIFY(recovery->isReady());
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    for (const auto& name : {"first", "second", "third", "fourth"}) {
        window.findChild<QAction*>("newQuery")->trigger();
        tabs->currentWidget()->setObjectName(name);
    }
    QCoreApplication::processEvents();
    auto* active = tabs->currentWidget();
    auto* bar = tabs->tabBar();
    const auto position = bar->tabRect(1).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, bar->mapToGlobal(position));
    QApplication::sendEvent(bar, &event);
    auto* menu = window.findChild<QMenu*>("editorTabContextMenu");
    QVERIFY(menu);
    QVERIFY(menu->isVisible());
    QCOMPARE(tabs->currentWidget(), active);
    QStringList labels;
    for (auto* item : menu->actions())
        labels.append(item->text());
    QCOMPARE(labels, (QStringList{"Close", "Close Others", "Close All", "Close to the Right"}));
    menu->actions().at(action)->trigger();
    QStringList actual;
    for (int index = 0; index < tabs->count(); ++index)
        actual.append(tabs->widget(index)->objectName());
    QCOMPARE(actual, remaining);
}

void NavigatorSqlWorkspaceTest::tabContextMenuDisablesEmptyGroupsAndIgnoresEmptySpace() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QCoreApplication::processEvents();
    auto* bar = tabs->tabBar();
    const auto empty = QPoint(bar->width() - 1, bar->tabRect(0).center().y());
    QCOMPARE(bar->tabAt(empty), -1);
    QContextMenuEvent emptyEvent(QContextMenuEvent::Mouse, empty, bar->mapToGlobal(empty));
    QApplication::sendEvent(bar, &emptyEvent);
    QVERIFY(!window.findChild<QMenu*>("editorTabContextMenu"));
    const auto position = bar->tabRect(0).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, bar->mapToGlobal(position));
    QApplication::sendEvent(bar, &event);
    auto* menu = window.findChild<QMenu*>("editorTabContextMenu");
    QVERIFY(menu);
    QCOMPARE(menu->actions().size(), 4);
    QVERIFY(menu->actions().at(0)->isEnabled());
    QVERIFY(!menu->actions().at(1)->isEnabled());
    QVERIFY(menu->actions().at(2)->isEnabled());
    QVERIFY(!menu->actions().at(3)->isEnabled());
}

void NavigatorSqlWorkspaceTest::tabContextMenuStopsBulkCloseWhenDiscardIsCancelled() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    window.findChild<QAction*>("newQuery")->trigger();
    auto* first = tabs->currentWidget();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* draft = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(draft);
    draft->setText("SELECT 'keep this draft';");
    draft->setModified(true);
    QCoreApplication::processEvents();
    auto* bar = tabs->tabBar();
    const auto position = bar->tabRect(0).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, bar->mapToGlobal(position));
    QApplication::sendEvent(bar, &event);
    auto* menu = window.findChild<QMenu*>("editorTabContextMenu");
    QVERIFY(menu);
    bool prompted = false;
    QTimer::singleShot(0, &window, [&] {
        auto* box =
            qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
        QVERIFY(box);
        prompted = true;
        box->button(QMessageBox::Cancel)->click();
    });
    menu->actions().at(2)->trigger();
    QVERIFY(prompted);
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->widget(0), first);
    QCOMPARE(tabs->widget(1), draft);
    QCOMPARE(draft->text(), QString("SELECT 'keep this draft';"));
    // The modal confirmation dismisses and deletes the original popup.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::sendEvent(bar, &event);
    menu = window.findChild<QMenu*>("editorTabContextMenu");
    QVERIFY(menu);
    QTimer::singleShot(0, &window, [&] {
        auto* box =
            qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
        QVERIFY(box);
        box->button(QMessageBox::Discard)->click();
    });
    menu->actions().at(2)->trigger();
    QCOMPARE(tabs->count(), 0);
}

void NavigatorSqlWorkspaceTest::unusedObjectInspectorDoesNotCoverSidebar() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QCoreApplication::processEvents();
    auto* inspector = window.findChild<choscordb::ObjectExplorer*>();
    QVERIFY(inspector);
    QVERIFY2(!inspector->isVisible(), "Unused inspector covers the sidebar at (0, 0)");
    auto* connections = window.findChild<QPushButton*>("sidebarConnections");
    const auto center = connections->mapTo(&window, connections->rect().center());
    QCOMPARE(window.childAt(center), connections);
}

void NavigatorSqlWorkspaceTest::resultsAutomaticallyShowGridOrFailureMessage() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    auto* views = window.findChild<QStackedWidget*>("sqlResultViews");
    auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
    auto* grid = window.findChild<QTableView*>("queryResults");
    QVERIFY(editor && views && messages && grid);
    QVERIFY(!window.findChild<QComboBox*>("resultViewSelector"));
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(window.findChild<QAction*>("runStatement")->isEnabled());
    editor->setText("SELECT 1 AS value;");
    window.findChild<QAction*>("runStatement")->trigger();
    QTRY_COMPARE(grid->model()->rowCount(), 1);
    QCOMPARE(views->currentWidget(), grid->parentWidget());
    QVERIFY(!grid->horizontalHeader()->stretchLastSection());
    editor->setText("SELECT * FROM missing_table;");
    window.findChild<QAction*>("runStatement")->trigger();
    QTRY_COMPARE(views->currentWidget(), messages);
    QTRY_VERIFY(messages->toPlainText().contains("missing_table"));
}

void NavigatorSqlWorkspaceTest::capturesSqlAndObjectTabsAtBothWorkspaceWidths() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QVERIFY(qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget()));
    for (const int width : {1280, 960}) {
        window.resize(width, width == 1280 ? 900 : 640);
        QCoreApplication::processEvents();
        QCOMPARE(window.width(), width);
        QVERIFY(window.grab().save(QString("unified-sql-%1.png").arg(width)));
    }
    emit window.objectContextSelected(17, R"(["main","preview"])", "main.preview", "table");
    QVERIFY(qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget()));
    QVERIFY(!window.findChild<QToolBar*>()->isVisible());
    for (const int width : {1280, 960}) {
        window.resize(width, width == 1280 ? 900 : 640);
        QCoreApplication::processEvents();
        QCOMPARE(window.width(), width);
        QVERIFY(window.grab().save(QString("unified-object-%1.png").arg(width)));
    }
}

void NavigatorSqlWorkspaceTest::sqlHeaderSitsBetweenWorkspaceTabsAndEditor() {
    choscordb::MainWindow window;
    window.resize(960, 640);
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* toolbar = window.findChild<QToolBar*>();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(toolbar && editor);
    const int tabBottom =
        tabs->tabBar()->mapTo(&window, tabs->tabBar()->tabRect(0).bottomLeft()).y();
    const int headerTop = toolbar->mapTo(&window, QPoint()).y();
    const int editorTop = editor->mapTo(&window, QPoint()).y();
    QVERIFY(headerTop > tabBottom);
    QVERIFY(headerTop + toolbar->height() <= editorTop);
}

void NavigatorSqlWorkspaceTest::workspaceTabsUseContentWidth() {
    choscordb::MainWindow window;
    window.resize(1280, 800);
    window.show();
    auto* add = window.findChild<QAction*>("newQuery");
    add->trigger();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QVERIFY(!tabs->tabBar()->expanding());
    QVERIFY(tabs->tabBar()->tabRect(0).width() < tabs->tabBar()->width() / 2);
}

void NavigatorSqlWorkspaceTest::sidebarPanelsSwitchWithoutChangingTheSqlTarget() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(editor);
    const auto target = editor->connectionTarget();
    auto* panels = window.findChild<QStackedWidget*>("sidebarPanels");
    auto* saved = window.findChild<QPushButton*>("sidebarSaved");
    auto* history = window.findChild<QPushButton*>("sidebarHistory");
    auto* connections = window.findChild<QPushButton*>("sidebarConnections");
    QVERIFY(panels && saved && history && connections);
    QCoreApplication::processEvents();
    QVERIFY(connections->width() > connections->height());
    QVERIFY(qAbs(connections->width() - saved->width()) <= 1);
    QVERIFY(qAbs(saved->width() - history->width()) <= 1);
    QCOMPARE(panels->currentIndex(), 0);
    saved->click();
    QCOMPARE(panels->currentIndex(), 1);
    history->click();
    QCOMPARE(panels->currentIndex(), 2);
    connections->click();
    QCOMPARE(panels->currentIndex(), 0);
    QCOMPARE(editor->connectionTarget(), target);
    QCOMPARE(tabs->currentWidget(), editor);
}

void NavigatorSqlWorkspaceTest::savedPanelFiltersFolderTreeAndReusesEditedTab() {
    QStandardPaths::setTestModeEnabled(true);
    const auto directory =
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath("com.choscor.ChoscorDB/sql");
    QVERIFY(QDir().mkpath(directory + "/nested"));
    const auto name = QStringLiteral("sidebar-test-%1.sql").arg(QCoreApplication::applicationPid());
    const auto path = QDir(directory).filePath(name);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("SELECT 13;"), qint64(10));
    file.close();
    QFile nested(QDir(directory + "/nested").filePath(name));
    QVERIFY(nested.open(QIODevice::WriteOnly));
    nested.write("SELECT 99;");
    nested.close();
    choscordb::MainWindow window;
    window.show();
    if (auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>())
        QTRY_VERIFY(recovery->isReady());
    window.findChild<QPushButton*>("sidebarSaved")->click();
    auto* list = window.findChild<QTreeWidget*>("sidebarSavedFiles");
    QVERIFY(list);
    auto* search = window.findChild<QLineEdit*>("sidebarSavedSearch");
    QVERIFY(search);
    const auto matches = list->findItems(name, Qt::MatchExactly | Qt::MatchRecursive);
    QCOMPARE(matches.size(), 2);
    auto* selected = matches.at(0)->parent() ? matches.at(1) : matches.at(0);
    QVERIFY(!selected->parent());
    auto* nestedItem = matches.at(0)->parent() ? matches.at(0) : matches.at(1);
    QCOMPARE(nestedItem->parent()->text(0), QString("nested"));
    search->setText("NESTED");
    QVERIFY(selected->isHidden());
    QVERIFY(!nestedItem->isHidden());
    QVERIFY(!nestedItem->parent()->isHidden());
    nestedItem->parent()->setExpanded(false);
    search->setText(name.toUpper());
    QVERIFY(nestedItem->parent()->isExpanded());
    QVERIFY(!nestedItem->isHidden());
    QVERIFY(!nestedItem->parent()->isHidden());
    QVERIFY(!selected->isHidden());
    search->setText("no-matching-file");
    QVERIFY(nestedItem->parent()->isHidden());
    search->clear();
    QVERIFY(!selected->isHidden());
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    const int beforeFolder = tabs->count();
    emit list->itemClicked(nestedItem->parent(), 0);
    QCOMPARE(tabs->count(), beforeFolder);
    QMetaObject::invokeMethod(list, "itemClicked", Q_ARG(QTreeWidgetItem*, selected),
                              Q_ARG(int, 0));
    QTRY_COMPARE(tabs->count(), 1);
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(editor);
    QTRY_COMPARE(editor->text(), QString("SELECT 13;"));
    editor->setText("unsaved edit");
    QMetaObject::invokeMethod(list, "itemClicked", Q_ARG(QTreeWidgetItem*, selected),
                              Q_ARG(int, 0));
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(editor->text(), QString("unsaved edit"));
    emit list->itemClicked(nestedItem, 0);
    QTRY_COMPARE(tabs->count(), 2);
    auto* nestedEditor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(nestedEditor && nestedEditor != editor);
    QTRY_COMPARE(nestedEditor->text(), QString("SELECT 99;"));
    QCOMPARE(editor->text(), QString("unsaved edit"));
    file.remove();
    nested.remove();
}

void NavigatorSqlWorkspaceTest::historySearchAppliesToRefreshedFullSql() {
    choscordb::MainWindow window;
    window.show();
    if (auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>())
        QTRY_VERIFY(recovery->isReady());
    auto* adapter = window.findChild<choscordb::QueryWorkspace*>()->adapter();
    QSignalSpy listed(adapter, &choscordb::EngineAdapter::historyListed);
    window.findChild<QPushButton*>("sidebarHistory")->click();
    QTRY_VERIFY(!listed.isEmpty());
    QTRY_VERIFY(window.findChild<QLabel*>("sidebarHistoryStatus")->text() !=
                QString::fromUtf8("Loading recent history…"));
    const auto token = listed.last().at(0).toULongLong();
    auto* search = window.findChild<QLineEdit*>("sidebarHistorySearch");
    auto* list = window.findChild<QListWidget*>("sidebarHistoryItems");
    QVERIFY(search && list);
    search->setText("needle");
    choscordb::SavedHistoryEntry matching, other;
    matching.id = "matching";
    matching.sql = QString(110, ' ') + "SELECT 'NEEDLE';";
    other.id = "other";
    other.sql = QString(110, ' ') + "SELECT 'other';";
    emit adapter->historyListed(token, {matching, other});
    QCOMPARE(list->count(), 2);
    QVERIFY(!list->item(0)->text().contains("NEEDLE"));
    QVERIFY(!list->item(0)->isHidden());
    QVERIFY(list->item(1)->isHidden());
    emit adapter->historyListed(token, {other});
    QCOMPARE(list->count(), 1);
    QVERIFY(list->item(0)->isHidden());
    search->clear();
    QVERIFY(!list->item(0)->isHidden());
}

void NavigatorSqlWorkspaceTest::historySidebarReusesRecordIdAndKeepsDistinctIdenticalSql() {
    choscordb::MainWindow window;
    window.show();
    if (auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>())
        QTRY_VERIFY(recovery->isReady());
    window.findChild<QPushButton*>("sidebarHistory")->click();
    auto* list = window.findChild<QListWidget*>("sidebarHistoryItems");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QVERIFY(list && tabs);
    QTRY_VERIFY(window.findChild<QLabel*>("sidebarHistoryStatus")->text() !=
                QString::fromUtf8("Loading recent history…"));
    choscordb::SavedHistoryEntry first, second;
    first.id = "history-one";
    first.sql = "SELECT 17;";
    first.profileId = "missing-profile";
    second = first;
    second.id = "history-two";
    auto* firstItem = new QListWidgetItem("First", list);
    firstItem->setData(Qt::UserRole, QVariant::fromValue(first));
    auto* secondItem = new QListWidgetItem("Second", list);
    secondItem->setData(Qt::UserRole, QVariant::fromValue(second));
    auto* search = window.findChild<QLineEdit*>("sidebarHistorySearch");
    QVERIFY(search);
    search->setText("FIRST");
    QVERIFY(!firstItem->isHidden());
    QVERIFY(secondItem->isHidden());
    search->setText("select 17");
    QVERIFY(!firstItem->isHidden());
    QVERIFY(!secondItem->isHidden());
    search->setText("no such query");
    QVERIFY(firstItem->isHidden());
    QVERIFY(secondItem->isHidden());
    search->clear();
    QVERIFY(!secondItem->isHidden());
    QMetaObject::invokeMethod(list, "itemClicked", Q_ARG(QListWidgetItem*, firstItem));
    QCOMPARE(tabs->count(), 1);
    auto* firstEditor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(firstEditor);
    QCOMPARE(firstEditor->connectionTarget(), std::optional<quint64>{});
    firstEditor->setText("edited history");
    QMetaObject::invokeMethod(list, "itemClicked", Q_ARG(QListWidgetItem*, firstItem));
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(firstEditor->text(), QString("edited history"));
    QMetaObject::invokeMethod(list, "itemClicked", Q_ARG(QListWidgetItem*, secondItem));
    QCOMPARE(tabs->count(), 2);
    auto* secondEditor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(secondEditor && secondEditor != firstEditor);
    QCOMPARE(secondEditor->text(), QString("SELECT 17;"));
    firstEditor->setModified(false);
    QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 0));
    QCOMPARE(tabs->count(), 1);
    QMetaObject::invokeMethod(list, "itemClicked", Q_ARG(QListWidgetItem*, firstItem));
    QCOMPARE(tabs->count(), 2);
    auto* reopened = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(reopened && reopened != firstEditor);
    QCOMPARE(reopened->text(), QString("SELECT 17;"));
    auto* fullHistory = window.findChild<choscordb::HistoryDock*>();
    QVERIFY(fullHistory);
    emit fullHistory->openRequested(first);
    QCOMPARE(tabs->count(), 3);
}

void NavigatorSqlWorkspaceTest::historyNavigationStaysInSidebar() {
    choscordb::MainWindow window;
    window.show();
    auto* panels = window.findChild<QStackedWidget*>("sidebarPanels");
    auto* screens = window.findChild<QStackedWidget*>("centralScreens");
    QVERIFY(panels && screens);
    auto* connections = window.findChild<QPushButton*>("sidebarConnections");
    auto* saved = window.findChild<QPushButton*>("sidebarSaved");
    auto* history = window.findChild<QPushButton*>("sidebarHistory");
    QVERIFY(connections && saved && history);
    QVERIFY(qAbs(connections->width() - saved->width()) <= 1);
    QVERIFY(qAbs(saved->width() - history->width()) <= 1);
    window.findChild<QAction*>("showHistory")->trigger();
    QCOMPARE(panels->currentIndex(), 2);
    QVERIFY(history->isChecked());
    QCOMPARE(screens->currentWidget()->objectName(), QString("startScreen"));
    QVERIFY(!window.findChild<QPushButton*>("sidebarFullHistory"));
}

void NavigatorSqlWorkspaceTest::recoveryActionsRemainInMenuWithoutToolButtons() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("workspace"));
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QVERIFY(recovery);
    auto* menu = window.findChild<QMenu*>("workspaceRecoveryMenu");
    QVERIFY(menu);
    for (const char* name : {"retryWorkspaceRecovery", "startNewWorkspace", "closeWithoutRecovery",
                             "cancelRecoveryClose"}) {
        QVERIFY(!window.findChild<QPushButton*>(name));
        auto* action = window.findChild<QAction*>(name);
        QVERIFY(action);
        QVERIFY(menu->actions().contains(action));
        QVERIFY(!action->isEnabled());
    }
    QVERIFY(QMetaObject::invokeMethod(recovery, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("Restore failed")),
                                      Q_ARG(bool, false)));
    QVERIFY(window.findChild<QAction*>("retryWorkspaceRecovery")->isEnabled());
    QVERIFY(window.findChild<QAction*>("startNewWorkspace")->isEnabled());
    QVERIFY(!window.findChild<QAction*>("closeWithoutRecovery")->isEnabled());
    QVERIFY(!window.findChild<QAction*>("cancelRecoveryClose")->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(recovery, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("Save failed")),
                                      Q_ARG(bool, true)));
    QVERIFY(!window.findChild<QAction*>("startNewWorkspace")->isEnabled());
    QVERIFY(window.findChild<QAction*>("closeWithoutRecovery")->isEnabled());
    QVERIFY(window.findChild<QAction*>("cancelRecoveryClose")->isEnabled());
    window.findChild<QAction*>("cancelRecoveryClose")->trigger();
    for (auto* action : menu->actions())
        QVERIFY(!action->isEnabled());
}

QTEST_MAIN(NavigatorSqlWorkspaceTest)
