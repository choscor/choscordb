#include "app/appearance_controller.h"
#include "app/main_window.h"
#include "app/navigator_controller.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
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
class NavigatorSqlWorkspaceTest : public QObject {
    Q_OBJECT
  private slots:
    void sqlRowActionsLiveInResultContextMenu() {
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
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
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
    void sqlOpensWithEqualEditorAndResults() {
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
    void sqlToolbarInsetsControls() {
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
    void tabContextMenuFollowsCursor() {
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
        QCOMPARE(menu->pos() + QPoint(margin, margin), cursor);
    }
    void tabContextMenuClosesRequestedDocuments_data() {
        QTest::addColumn<int>("action");
        QTest::addColumn<QStringList>("remaining");
        QTest::newRow("close") << 0 << QStringList{"first", "third", "fourth"};
        QTest::newRow("others") << 1 << QStringList{"second"};
        QTest::newRow("all") << 2 << QStringList{};
        QTest::newRow("right") << 3 << QStringList{"first", "second"};
    }
    void tabContextMenuClosesRequestedDocuments() {
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
    void tabContextMenuDisablesEmptyGroupsAndIgnoresEmptySpace() {
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
    void tabContextMenuStopsBulkCloseWhenDiscardIsCancelled() {
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
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
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
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            box->button(QMessageBox::Discard)->click();
        });
        menu->actions().at(2)->trigger();
        QCOMPARE(tabs->count(), 0);
    }
    void unusedObjectInspectorDoesNotCoverSidebar() {
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
    void resultsAutomaticallyShowGridOrFailureMessage() {
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
    void capturesSqlAndObjectTabsAtBothWorkspaceWidths() {
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
    void sqlHeaderSitsBetweenWorkspaceTabsAndEditor() {
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
    void workspaceTabsUseContentWidth() {
        choscordb::MainWindow window;
        window.resize(1280, 800);
        window.show();
        auto* add = window.findChild<QAction*>("newQuery");
        add->trigger();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        QVERIFY(!tabs->tabBar()->expanding());
        QVERIFY(tabs->tabBar()->tabRect(0).width() < tabs->tabBar()->width() / 2);
    }
    void sidebarPanelsSwitchWithoutChangingTheSqlTarget() {
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
    void savedPanelFiltersFolderTreeAndReusesEditedTab() {
        QStandardPaths::setTestModeEnabled(true);
        const auto directory =
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                .filePath("sql");
        QVERIFY(QDir().mkpath(directory + "/nested"));
        const auto name =
            QStringLiteral("sidebar-test-%1.sql").arg(QCoreApplication::applicationPid());
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
    void historySearchAppliesToRefreshedFullSql() {
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
    void historySidebarReusesRecordIdAndKeepsDistinctIdenticalSql() {
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
    void historyNavigationStaysInSidebar() {
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
    void recoveryActionsRemainInMenuWithoutToolButtons() {
        QTemporaryDir storage;
        choscordb::MainWindow window(nullptr, storage.filePath("workspace"));
        window.show();
        auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
        QVERIFY(recovery);
        auto* menu = window.findChild<QMenu*>("workspaceRecoveryMenu");
        QVERIFY(menu);
        for (const char* name : {"retryWorkspaceRecovery", "startNewWorkspace",
                                 "closeWithoutRecovery", "cancelRecoveryClose"}) {
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
    void objectTabsUseConnectionAndQualifiedIdentity() {
        choscordb::MainWindow window;
        window.show();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        QVERIFY(tabs);
        const int initial = tabs->count();
        QCOMPARE(initial, 0);
        emit window.objectContextSelected(11, R"(["main","account"])", "main.account", "table");
        QCOMPARE(tabs->count(), initial + 1);
        auto* first = qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget());
        QVERIFY(first);
        QCOMPARE(first->paneIndex(), 4);
        first->selectPane(3);
        emit window.objectContextSelected(11, R"(["other","account"])", "other.account", "table");
        QCOMPARE(tabs->count(), initial + 2);
        emit window.objectContextSelected(22, R"(["main","account"])", "main.account", "table");
        QCOMPARE(tabs->count(), initial + 3);
        emit window.objectContextSelected(11, R"(["main","account"])", "main.account", "table");
        QCOMPARE(tabs->count(), initial + 3);
        QCOMPARE(tabs->currentWidget(), first);
        QCOMPARE(first->paneIndex(), 4);
        window.findChild<QAction*>("closeWorkspaceTab")->trigger();
        QCOMPARE(tabs->count(), initial + 2);
        while (tabs->count())
            tabs->tabCloseRequested(tabs->currentIndex());
        QCOMPARE(window.findChild<QStackedWidget*>("centralScreens")->currentWidget()->objectName(),
                 QString("startScreen"));
    }
    void sidebarViewsKeepTheirDefaultPane() {
        choscordb::MainWindow window;
        window.show();
        emit window.objectContextSelected(11, R"(["main","summary"])", "summary", "view");
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* explorer = qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget());
        QVERIFY(explorer);
        QCOMPARE(explorer->paneIndex(), 0);
        explorer->selectPane(3);
        emit window.objectContextSelected(11, R"(["main","summary"])", "summary", "view");
        QCOMPARE(explorer->paneIndex(), 3);
    }
    void savedProfileIdCannotCollideWithSessionContext() {
        choscordb::MainWindow window;
        window.show();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        workspace->connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 1);
        const auto session = connected.at(0).at(0).toULongLong();
        choscordb::SavedProfile profile;
        profile.id = QStringLiteral("session:%1").arg(session);
        profile.name = "Named collision";
        profile.path = ":memory:";
        const auto saved = workspace->connectSavedProfile(profile);
        QVERIFY(saved);
        QTRY_COMPARE(connected.count(), 2);
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        emit window.objectContextSelected(session, R"(["main","same"])", "main.same", "table");
        QTRY_VERIFY(workspace->navigationAllowed());
        emit window.objectContextSelected(*saved, R"(["main","same"])", "main.same", "table");
        QCOMPARE(tabs->count(), 2);
        QVERIFY(tabs->widget(0)->property("objectProfileId").toString().startsWith("session:"));
        QVERIFY(tabs->widget(1)->property("objectProfileId").toString().startsWith("profile:"));
    }
    void selectedConnectionShowsOnlyItsTree() {
        choscordb::EngineAdapter adapter;
        QTreeView tree;
        QLineEdit filter;
        choscordb::NavigatorController navigator(&adapter, &tree, &filter, &tree);
        QObject::disconnect(navigator.model(), &choscordb::NavigatorModel::childrenRequested,
                            &adapter, &choscordb::EngineAdapter::loadMetadata);
        navigator.addConnection(11, "First");
        navigator.addConnection(22, "Second");
        navigator.setSelectedConnection(11);
        QCOMPARE(tree.model()->rowCount(), 1);
        QCOMPARE(tree.model()->index(0, 0).data().toString(), QString("First"));
        navigator.setSelectedConnection(22);
        QCOMPARE(tree.model()->rowCount(), 1);
        QCOMPARE(tree.model()->index(0, 0).data().toString(), QString("Second"));
        navigator.clearSelectedConnection();
        QCOMPARE(tree.model()->rowCount(), 0);
    }
    void searchLoadsCollapsedGroupsOnlyForSelectedConnection() {
        choscordb::EngineAdapter adapter;
        QTreeView tree;
        QLineEdit filter;
        choscordb::NavigatorController navigator(&adapter, &tree, &filter, &tree);
        QObject::disconnect(navigator.model(), &choscordb::NavigatorModel::childrenRequested,
                            &adapter, &choscordb::EngineAdapter::loadMetadata);
        navigator.addConnection(11, "First");
        navigator.addConnection(22, "Second");
        navigator.setSelectedConnection(11);
        connect(navigator.model(), &choscordb::NavigatorModel::childrenRequested, &tree,
                [&](quint64 connection, const QString& parent, quint64 token) {
                    if (connection != 11)
                        return;
                    std::vector<choscordb::NavigatorObject> objects;
                    if (parent.isEmpty())
                        objects.push_back({"schema", "public", "public", "schema", true});
                    else if (parent == "schema")
                        objects.push_back({"group", "Tables", "", "group", true});
                    else if (parent == "group")
                        objects.push_back({"table", "needle", "public.needle", "table", false});
                    navigator.model()->applyChildren(connection, parent, token, std::move(objects));
                });
        filter.setText("needle");
        QTRY_VERIFY(navigator.model()
                        ->index(0, 0)
                        .data(choscordb::NavigatorModel::ChildrenLoadedRole)
                        .toBool());
        QTRY_COMPARE(tree.model()->rowCount(), 1);
        auto root = tree.model()->index(0, 0);
        QTRY_COMPARE(tree.model()->rowCount(root), 1);
        auto schema = tree.model()->index(0, 0, root);
        QTRY_COMPARE(tree.model()->rowCount(schema), 1);
        auto group = tree.model()->index(0, 0, schema);
        QTRY_COMPARE(tree.model()->index(0, 0, group).data().toString(), QString("needle"));
    }
    void searchFailureKeepsRefineMessage() {
        choscordb::EngineAdapter adapter;
        QTreeView tree;
        QLineEdit filter;
        choscordb::NavigatorController navigator(&adapter, &tree, &filter, &tree);
        QObject::disconnect(navigator.model(), &choscordb::NavigatorModel::childrenRequested,
                            &adapter, &choscordb::EngineAdapter::loadMetadata);
        navigator.addConnection(11, "First");
        navigator.setSelectedConnection(11);
        QSignalSpy status(&navigator, &choscordb::NavigatorController::searchStatusChanged);
        connect(navigator.model(), &choscordb::NavigatorModel::childrenRequested, &tree,
                [&adapter, &status](quint64 connection, const QString& parent, quint64 token) {
                    emit adapter.metadataSubmissionFailed(connection, parent, token + 1,
                                                          "Stale error");
                    QCOMPARE(status.last().first().toString(), QString("Searching objects…"));
                    emit adapter.metadataSubmissionFailed(connection, parent, token,
                                                          "Metadata limit exceeded");
                });
        filter.setText("needle");
        QTRY_VERIFY(!status.isEmpty() &&
                    status.last().first().toString().contains("Refine the text"));
        QCoreApplication::processEvents();
        QVERIFY(status.last().first().toString().contains("Metadata limit exceeded"));
    }
    void selectingObjectLoadsMetadataAndSeparateDataThenReturnsToSql() {
        choscordb::MainWindow window;
        window.show();
        window.findChild<QAction*>("newQuery")->trigger();
        QTRY_VERIFY(window.findChild<choscordb::AppearanceController*>()->isReady());
        window.resize(960, 640);
        QCOMPARE(window.size(), QSize(960, 640));
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        int finished = 0;
        connect(
            workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& event) {
                if (event.kind == "query_finished")
                    ++finished;
            },
            Qt::DirectConnection);
        workspace->connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 1);
        const auto connection = connected.first().first().toULongLong();
        auto create = workspace->adapter()->execute(
            connection, "CREATE TABLE \"Ui table\"(value INTEGER DEFAULT 7)");
        QVERIFY(create);
        workspace->adapter()->fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        auto insert =
            workspace->adapter()->execute(connection, "INSERT INTO \"Ui table\" VALUES(7)");
        QVERIFY(insert);
        workspace->adapter()->fetchPage(*insert);
        QTRY_COMPARE(finished, 2);
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        editor->setText("SELECT 99");
        auto* run = window.findChild<QAction*>("runStatement");
        QTRY_VERIFY(run->isEnabled());
        run->trigger();
        QTRY_COMPARE(finished, 3);
        auto* sql = window.findChild<QTableView*>("queryResults");
        QCOMPARE(sql->model()->index(0, 0).data().toString(), QString("99"));
        auto* navigator = window.findChild<choscordb::NavigatorController*>();
        navigator->setSelectedConnection(connection);
        auto* model = navigator->model();
        auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_VERIFY(root.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
        auto schema = model->index(0, 0, root);
        model->fetchMore(schema);
        QTRY_VERIFY(schema.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
        auto group = model->index(0, 0, schema);
        QCOMPARE(group.data(choscordb::NavigatorModel::KindRole).toString(), QString("group"));
        model->fetchMore(group);
        QTRY_VERIFY(group.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
        auto table = model->index(0, 0, group);
        QCOMPARE(table.data().toString(), QString("Ui table"));
        auto* tree = window.findChild<QTreeView*>("databaseNavigator");
        auto* proxy = qobject_cast<QSortFilterProxyModel*>(tree->model());
        QVERIFY(proxy);
        tree->expand(proxy->mapFromSource(root));
        tree->expand(proxy->mapFromSource(schema));
        tree->expand(proxy->mapFromSource(group));
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                          tree->visualRect(proxy->mapFromSource(table)).center());
        auto* screens = window.findChild<QStackedWidget*>("centralScreens");
        auto* workspaceTabs = window.findChild<QTabWidget*>("editorTabs");
        QCOMPARE(screens->currentWidget()->objectName(), QString("sqlScreen"));
        QCOMPARE(workspaceTabs->count(), 2);
        QTRY_VERIFY(qobject_cast<choscordb::ObjectExplorer*>(workspaceTabs->currentWidget()));
        QVERIFY(window.findChild<QWidget*>("objectHeader")->isVisible());
        QVERIFY(!window.findChild<QWidget*>("sqlResultViews")->isVisible());
        auto* panes = window.findChild<QTabBar*>("objectTabs");
        QCOMPARE(panes->currentIndex(), 4);
        auto* data = window.findChild<QTableView*>("objectDataResults");
        QVERIFY(data);
        QTRY_COMPARE(data->model()->rowCount(), 1);
        QCOMPARE(data->model()->index(0, 0).data().toString(), QString("7"));
        QTest::mouseClick(panes, Qt::LeftButton, Qt::NoModifier, panes->tabRect(0).center());
        auto* metadata = window.findChild<QTableView*>("objectMetadata");
        QVERIFY(metadata);
        QTRY_COMPARE(metadata->model()->rowCount(), 1);
        QCOMPARE(metadata->model()->index(0, 0).data().toString(), QString("value"));
        QSignalSpy extraReads(workspace->adapter(),
                              &choscordb::EngineAdapter::objectInspectionReady);
        emit workspace->connectionReady(connection);
        QCOMPARE(metadata->model()->rowCount(), 1);
        QCOMPARE(extraReads.count(), 0);
        QTest::mouseClick(panes, Qt::LeftButton, Qt::NoModifier, panes->tabRect(4).center());
        QTRY_COMPARE(data->model()->rowCount(), 1);
        QCOMPARE(data->model()->index(0, 0).data().toString(), QString("7"));
        auto* dataExport = window.findChild<QPushButton*>("objectDataExport");
        auto* openObjectQuery = window.findChild<QPushButton*>("objectOpenQuery");
        QTRY_COMPARE(dataExport->mapTo(&window, dataExport->rect().center()).y(),
                     openObjectQuery->mapTo(&window, openObjectQuery->rect().center()).y());
        QVERIFY(window.findChild<QPushButton*>("objectRefresh")->isVisible());
        QVERIFY(!window.findChild<QPushButton*>("objectRefresh")->isEnabled());
        QVERIFY(!window.findChild<QLabel*>("objectStatus")->isVisible());
        QTest::mouseClick(panes, Qt::LeftButton, Qt::NoModifier, panes->tabRect(0).center());
        QTRY_VERIFY(window.findChild<QPushButton*>("objectRefresh")->isVisible());
        QVERIFY(dataExport->isVisible());
        QVERIFY(!dataExport->isEnabled());
        QTest::mouseClick(panes, Qt::LeftButton, Qt::NoModifier, panes->tabRect(4).center());
        QTRY_VERIFY(dataExport->isVisible());
        QTRY_VERIFY(dataExport->isEnabled());
        QTRY_VERIFY(workspace->navigationAllowed());
        window.findChild<QAction*>("showSql")->trigger();
        QTRY_VERIFY(run->isEnabled());
        QVERIFY(window.findChild<QWidget*>("sqlResultViews")->isVisible());
        QCOMPARE(screens->currentWidget()->objectName(), QString("sqlScreen"));
        QCOMPARE(editor->text(), QString("SELECT 99"));
        QCOMPARE(sql->model()->index(0, 0).data().toString(), QString("99"));
    }
    void objectDataKeepsCancelPaneVisibleUntilAcknowledged() {
        choscordb::MainWindow window;
        window.show();
        window.findChild<QAction*>("newQuery")->trigger();
        QTRY_VERIFY(window.findChild<choscordb::AppearanceController*>()->isReady());
        window.resize(960, 640);
        QCOMPARE(window.size(), QSize(960, 640));
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        int finished = 0;
        connect(
            workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& event) {
                if (event.kind == "query_finished")
                    ++finished;
            },
            Qt::DirectConnection);
        workspace->connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 1);
        const auto connection = connected.first().first().toULongLong();
        auto create = workspace->adapter()->execute(
            connection, "CREATE VIEW slow AS WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 "
                        "FROM n WHERE x<100000000) SELECT sum(x) AS total FROM n");
        QVERIFY(create);
        workspace->adapter()->fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        emit window.objectContextSelected(connection, R"(["main","slow"])", "\"main\".\"slow\"",
                                          "view");
        auto* explorer = qobject_cast<choscordb::ObjectExplorer*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        QVERIFY(explorer);
        auto* panes = explorer->findChild<QTabBar*>("objectTabs");
        QTest::mouseClick(panes, Qt::LeftButton, Qt::NoModifier, panes->tabRect(4).center());
        auto* cancel = window.findChild<QPushButton*>("objectDataCancel");
        QTRY_VERIFY(cancel->isEnabled());
        auto* workspaceTabs = window.findChild<QTabWidget*>("editorTabs");
        workspaceTabs->setCurrentIndex(0);
        QCOMPARE(workspaceTabs->currentWidget(), explorer);
        QTest::mouseClick(panes, Qt::LeftButton, Qt::NoModifier, panes->tabRect(0).center());
        QCOMPARE(panes->currentIndex(), 4);
        QVERIFY(explorer->findChild<QLabel*>("objectStatus")
                    ->text()
                    .contains("cancel", Qt::CaseInsensitive));
        QVERIFY(window.findChild<choscordb::ToastRegion*>("toastRegion")
                    ->text()
                    .contains("cancel", Qt::CaseInsensitive));
        QCOMPARE(window.findChild<choscordb::ToastRegion*>("toastRegion")
                     ->property("variant")
                     .toString(),
                 QString("warning"));
        QVERIFY(!window.showScreen(choscordb::MainWindow::Screen::Start));
        QSignalSpy changed(explorer, &choscordb::ObjectExplorer::objectChanged);
        explorer->openObject(connection, R"(["main","another"])", "another");
        QCOMPARE(changed.count(), 0);
        QVERIFY(cancel->isVisible());
        QVERIFY(window.rect().contains(QRect(cancel->mapTo(&window, QPoint()), cancel->size())));
        QVERIFY(!cancel->visibleRegion().isEmpty());
        QTest::mouseClick(cancel, Qt::LeftButton);
        QTRY_VERIFY(workspace->navigationAllowed());
        QTest::mouseClick(panes, Qt::LeftButton, Qt::NoModifier, panes->tabRect(0).center());
        QCOMPARE(panes->currentIndex(), 0);
        QTRY_COMPARE(explorer->findChild<QTableView*>("objectMetadata")->model()->rowCount(), 1);
    }
    void documentsKeepTargetsAndImmutableResultOrigin() {
        QTemporaryDir directory;
        choscordb::MainWindow window;
        window.show();
        window.findChild<QAction*>("newQuery")->trigger();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        auto* selector = window.findChild<QComboBox*>("connectionSelector");
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* run = window.findChild<QAction*>("runStatement");
        auto* grid = window.findChild<QTableView*>("queryResults");
        auto* summary = window.findChild<QLabel*>("executionSummary");
        int queued = 0;
        connect(
            workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& event) {
                if (QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size())) ==
                        "query_state" &&
                    QString::fromUtf8(event.state.data(), qsizetype(event.state.size())) ==
                        "queued")
                    ++queued;
            },
            Qt::DirectConnection);
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        workspace->connectSqlite(directory.filePath("first.sqlite"));
        QTRY_COMPARE(connected.count(), 1);
        const auto firstConnection = selector->currentData();
        auto* first = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        first->setText("SELECT file FROM pragma_database_list WHERE name = 'main';");
        QTRY_VERIFY(run->isEnabled());
        run->trigger();
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        QCOMPARE(QFileInfo(grid->model()->data(grid->model()->index(0, 0)).toString())
                     .canonicalFilePath(),
                 QFileInfo(directory.filePath("first.sqlite")).canonicalFilePath());
        QTRY_VERIFY(run->isEnabled());
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Completed in"));
        const auto origin = summary->text();
        QVERIFY(origin.contains(tabs->tabText(0).remove(" •")));
        QVERIFY(origin.contains("first.sqlite"));
        workspace->connectSqlite(directory.filePath("second.sqlite"));
        QTRY_COMPARE(connected.count(), 2);
        QCOMPARE(selector->currentData(), firstConnection);
        first->setText(QString("-- retained line\n").repeated(100));
        first->SendScintilla(QsciScintilla::SCI_APPENDTEXT, 8UL, "-- edit\n");
        first->setSelection(45, 3, 45, 9);
        first->setFirstVisibleLine(40);
        const auto firstText = first->text();
        const auto selection = first->selectedText();
        const auto scroll = first->firstVisibleLine();
        QVERIFY(first->isUndoAvailable());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* second = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QVERIFY(second != first);
        selector->setCurrentIndex(selector->count() - 1);
        const auto secondConnection = selector->currentData();
        second->setText("SELECT file FROM pragma_database_list WHERE name = 'main';");
        tabs->setCurrentWidget(first);
        QCOMPARE(selector->currentData(), firstConnection);
        QCOMPARE(first->text(), firstText);
        QCOMPARE(first->selectedText(), selection);
        QCOMPARE(first->firstVisibleLine(), scroll);
        QVERIFY(first->isUndoAvailable());
        first->undo();
        QCOMPARE(first->text(), QString("-- retained line\n").repeated(100));
        QCOMPARE(summary->text(), origin);
        tabs->setCurrentWidget(second);
        QCOMPARE(selector->currentData(), secondConnection);
        QCOMPARE(summary->text(), origin);
        run->trigger();
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        QTRY_COMPARE(grid->model()->data(grid->model()->index(0, 0)).toString(),
                     QFileInfo(directory.filePath("second.sqlite")).canonicalFilePath());
        QTRY_VERIFY(run->isEnabled());
        QVERIFY(summary->text().contains("second.sqlite"));
        QVERIFY(summary->text() != origin);
        QCOMPARE(queued, 2);
    }
    void activeExecutionKeepsDocumentAndCancelReachable() {
        choscordb::MainWindow window;
        window.show();
        window.findChild<QAction*>("newQuery")->trigger();
        QTRY_VERIFY(window.findChild<choscordb::AppearanceController*>()->isReady());
        window.resize(960, 640);
        QCOMPARE(window.size(), QSize(960, 640));
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* run = window.findChild<QAction*>("runStatement");
        workspace->connectSqlite(":memory:");
        QTRY_VERIFY(run->isEnabled());
        auto* first = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* second = tabs->currentWidget();
        tabs->setCurrentWidget(first);
        first->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                       "x<100000000) SELECT sum(x) FROM n;");
        run->trigger();
        QVERIFY(!run->isEnabled());
        auto* progress = window.findChild<choscordb::ToastRegion*>("progressToast");
        QVERIFY(progress);
        QVERIFY(progress->isVisible());
        QVERIFY(progress->findChild<QProgressBar*>()->isVisible());
        QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                          tabs->tabBar()->tabRect(1).center());
        QCOMPARE(tabs->currentWidget(), first);
        QVERIFY(window.findChild<choscordb::ToastRegion*>("toastRegion")
                    ->text()
                    .contains("cancel", Qt::CaseInsensitive));
        first->setFocus();
        QTest::keyClick(first, Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(tabs->currentWidget(), first);
        window.findChild<QAction*>("listEditorTabs")->trigger();
        auto* overflow = window.findChild<QMenu*>("editorTabOverflow");
        QVERIFY(overflow);
        overflow->actions().at(1)->trigger();
        overflow->close();
        QCOMPARE(tabs->currentWidget(), first);
        window.findChild<QAction*>("newQuery")->trigger();
        QCOMPARE(tabs->count(), 2);
        tabs->setCurrentWidget(second);
        QCOMPARE(tabs->currentWidget(), first);
        auto* cancel = window.findChild<QAction*>("command_cancel_query");
        QVERIFY(cancel);
        QVERIFY(cancel->isEnabled());
        auto* cancelButton = window.findChild<QPushButton*>("cancelQueryButton");
        QVERIFY(cancelButton);
        QVERIFY(cancelButton->isVisible());
        QVERIFY(!cancelButton->visibleRegion().isEmpty());
        QVERIFY(window.rect().contains(
            QRect(cancelButton->mapTo(&window, QPoint()), cancelButton->size())));
        QTest::mouseClick(cancelButton, Qt::LeftButton);
        QCOMPARE(cancel->text(), QString("Cancelling…"));
        QCOMPARE(window.findChild<QLabel*>("executionSummary")->property("state").toString(),
                 QString("cancelling"));
        QVERIFY(!run->isEnabled());
        QVERIFY(!cancel->isEnabled());
        QTRY_VERIFY(run->isEnabled());
        tabs->setCurrentWidget(second);
        QCOMPARE(tabs->currentWidget(), second);
    }
    void disconnectMenuKeepsOtherSessionsAndDrafts() {
        choscordb::MainWindow window;
        window.show();
        window.findChild<QAction*>("newQuery")->trigger();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        workspace->connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 1);
        workspace->connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 2);
        const auto second = connected.at(1).at(0).toULongLong();
        auto* navigator = window.findChild<choscordb::NavigatorController*>();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        editor->setText("-- keep this draft");
        QMenu menu;
        navigator->populateContextMenu(&menu, navigator->model()->index(0, 0));
        auto* action = menu.findChild<QAction*>("disconnectSession");
        QVERIFY(action);
        bool observed = false;
        QTimer::singleShot(0, &window, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box)
                return;
            observed = true;
            QVERIFY(box->grab().save("native-disconnect.png"));
            QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel));
            box->button(QMessageBox::Cancel)->click();
        });
        action->trigger();
        QVERIFY(observed);
        QCOMPARE(navigator->model()->rowCount(), 2);
        QTimer::singleShot(0, &window, [] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            for (auto* button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
                    button->click();
                    return;
                }
            box->reject();
        });
        action->trigger();
        QTRY_COMPARE(navigator->model()->rowCount(), 1);
        auto* selector = window.findChild<QComboBox*>("connectionSelector");
        QVERIFY(!selector->currentData().isValid());
        QVERIFY(selector->placeholderText().contains("Disconnected"));
        auto* run = window.findChild<QAction*>("runStatement");
        QVERIFY(!run->isEnabled());
        QCOMPARE(editor->text(), QString("-- keep this draft"));
        selector->setCurrentIndex(selector->findData(QVariant::fromValue<qulonglong>(second)));
        QTRY_VERIFY(run->isEnabled());
        QCOMPARE(selector->currentData().toULongLong(), second);
    }
    void generationOpensDraftOnExistingSavedConnectionWithoutExecuting() {
        choscordb::MainWindow window;
        window.show();
        window.findChild<QAction*>("newQuery")->trigger();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        for (auto* action : window.findChildren<QAction*>())
            if (action->text() == "New SQLite session…")
                action->trigger();
        auto* profiles = window.findChild<choscordb::ProfileDialog*>();
        QVERIFY(profiles);
        auto* save = profiles->findChild<QPushButton*>("profileSave");
        QTRY_VERIFY(save->isEnabled());
        choscordb::SavedProfile profile;
        profile.id = "generated-sql-profile";
        profile.name = "Generated SQL fixture";
        profile.path = ":memory:";
        profiles->saveDraft(profile);
        QTRY_COMPARE(profiles->findChild<QListWidget*>("profileList")->count(), 1);
        QTRY_VERIFY(save->isEnabled());
        profiles->selectProfile(profile.id);
        profiles->findChild<QPushButton*>("profileConnect")->click();
        QTRY_COMPARE(connected.count(), 1);
        profiles->hide();
        const auto id = connected.first().at(0).toULongLong();
        int started = 0, finished = 0;
        auto* adapter = workspace->adapter();
        connect(
            adapter, &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& event) {
                const auto kind =
                    QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
                if (kind == "query_state" &&
                    QString::fromUtf8(event.state.data(), qsizetype(event.state.size())) ==
                        "queued")
                    ++started;
                if (kind == "query_finished")
                    ++finished;
            },
            Qt::DirectConnection);
        auto query = adapter->execute(id, "CREATE TABLE \"a.b\" (\"col name\" TEXT)");
        QVERIFY(query);
        adapter->fetchPage(*query);
        QTRY_COMPARE(finished, 1);
        QCOMPARE(started, 1);
        adapter->releaseQuery(*query);
        auto* navigator = window.findChild<choscordb::NavigatorController*>();
        auto* model = navigator->model();
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_VERIFY(root.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
        const auto schema = model->index(0, 0, root);
        model->fetchMore(schema);
        QTRY_VERIFY(schema.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
        const auto group = model->index(0, 0, schema);
        QCOMPARE(group.data(choscordb::NavigatorModel::KindRole).toString(), QString("group"));
        model->fetchMore(group);
        QTRY_VERIFY(group.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
        const auto table = model->index(0, 0, group);
        QCOMPARE(table.data().toString(), QString("a.b"));
        model->fetchMore(table);
        QTRY_VERIFY(table.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
        workspace->connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 2);
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* original = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        original->setText("-- preserved draft");
        const int tabCount = tabs->count(), queryCount = started;
        QMenu menu;
        navigator->populateContextMenu(&menu, table);
        auto* showDdl = [&menu] {
            for (auto* action : menu.actions())
                if (action->text() == QStringLiteral("Show DDL"))
                    return action;
            return static_cast<QAction*>(nullptr);
        }();
        QVERIFY(showDdl);
        showDdl->trigger();
        auto* ddlObject = qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget());
        QVERIFY(ddlObject);
        QCOMPARE(ddlObject->paneIndex(), 3);
        QTRY_VERIFY(ddlObject->findChild<QPlainTextEdit*>("objectDdl")
                        ->toPlainText()
                        .contains("CREATE TABLE"));
        QCOMPARE(QApplication::activeModalWidget(), nullptr);
        tabs->tabCloseRequested(tabs->currentIndex());
        tabs->setCurrentWidget(original);
        auto* select = menu.findChild<QAction*>("generate_select");
        QVERIFY(select);
        select->trigger();
        QCOMPARE(tabs->count(), tabCount + 1);
        auto* toast = window.findChild<choscordb::ToastRegion*>("toastRegion");
        QVERIFY(toast);
        QCOMPARE(toast->property("variant").toString(), QString("success"));
        QVERIFY(toast->text().contains("Review the draft before running."));
        auto* generated = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QCOMPARE(generated->text(), QString("SELECT * FROM \"main\".\"a.b\";"));
        QVERIFY(generated->isModified());
        QCOMPARE(generated->property("profileId").toString(), profile.id);
        QCOMPARE(window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong(),
                 id);
        QCOMPARE(original->text(), QString("-- preserved draft"));
        menu.findChild<QAction*>("generate_insert")->trigger();
        QCOMPARE(tabs->count(), tabCount + 2);
        auto* insert = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QVERIFY(
            insert->text().contains("INSERT INTO \"main\".\"a.b\" (\"col name\") VALUES ($1);"));
        QVERIFY(insert->text().startsWith("--"));
        menu.findChild<QAction*>("generate_update")->trigger();
        QCOMPARE(tabs->count(), tabCount + 3);
        auto* update = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QVERIFY(update->text().contains(
            "UPDATE \"main\".\"a.b\" SET \"col name\" = $1 WHERE /* predicate */;"));
        menu.findChild<QAction*>("generate_delete")->trigger();
        QCOMPARE(tabs->count(), tabCount + 4);
        auto* remove = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QCOMPARE(remove->text(), QString("DELETE FROM \"main\".\"a.b\" WHERE /* predicate */;"));
        tabs->setCurrentWidget(insert);
        QCOMPARE(started, queryCount);
        QCOMPARE(connected.count(), 2);
        QVERIFY(window.grab().save("native-navigator-sql.png"));
        QSignalSpy shutdown(adapter, &choscordb::EngineAdapter::shutdownReady);
        adapter->beginShutdown();
        QTRY_COMPARE(shutdown.count(), 1);
        // Actor disconnects and the metadata barrier drain accepted work, proving
        // no generated draft submitted another queued query even asynchronously.
        QCOMPARE(started, queryCount);
    }
};
QTEST_MAIN(NavigatorSqlWorkspaceTest)
#include "navigator_sql_workspace_test.moc"
