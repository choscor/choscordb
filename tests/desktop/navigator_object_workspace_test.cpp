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
#include "navigator_sql_workspace_test.h"
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
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest>
#include <cstdint>

void NavigatorSqlWorkspaceTest::objectTabsUseConnectionAndQualifiedIdentity() {
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

void NavigatorSqlWorkspaceTest::sidebarViewsKeepTheirDefaultPane() {
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

void NavigatorSqlWorkspaceTest::savedProfileIdCannotCollideWithSessionContext() {
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

void NavigatorSqlWorkspaceTest::selectedConnectionShowsOnlyItsTree() {
    choscordb::EngineAdapter adapter;
    QTreeView tree;
    QLineEdit filter;
    choscordb::NavigatorController navigator(&adapter, &tree, &filter, &tree);
    QObject::disconnect(navigator.model(), &choscordb::NavigatorModel::childrenRequested, &adapter,
                        &choscordb::EngineAdapter::loadMetadata);
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

void NavigatorSqlWorkspaceTest::searchLoadsCollapsedGroupsOnlyForSelectedConnection() {
    choscordb::EngineAdapter adapter;
    QTreeView tree;
    QLineEdit filter;
    choscordb::NavigatorController navigator(&adapter, &tree, &filter, &tree);
    QObject::disconnect(navigator.model(), &choscordb::NavigatorModel::childrenRequested, &adapter,
                        &choscordb::EngineAdapter::loadMetadata);
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

void NavigatorSqlWorkspaceTest::searchFailureKeepsRefineMessage() {
    choscordb::EngineAdapter adapter;
    QTreeView tree;
    QLineEdit filter;
    choscordb::NavigatorController navigator(&adapter, &tree, &filter, &tree);
    QObject::disconnect(navigator.model(), &choscordb::NavigatorModel::childrenRequested, &adapter,
                        &choscordb::EngineAdapter::loadMetadata);
    navigator.addConnection(11, "First");
    navigator.setSelectedConnection(11);
    QSignalSpy status(&navigator, &choscordb::NavigatorController::searchStatusChanged);
    connect(navigator.model(), &choscordb::NavigatorModel::childrenRequested, &tree,
            [&adapter, &status](quint64 connection, const QString& parent, quint64 token) {
                emit adapter.metadataSubmissionFailed(connection, parent, token + 1, "Stale error");
                QCOMPARE(status.last().first().toString(), QString("Searching objects…"));
                emit adapter.metadataSubmissionFailed(connection, parent, token,
                                                      "Metadata limit exceeded");
            });
    filter.setText("needle");
    QTRY_VERIFY(!status.isEmpty() && status.last().first().toString().contains("Refine the text"));
    QCoreApplication::processEvents();
    QVERIFY(status.last().first().toString().contains("Metadata limit exceeded"));
}

void NavigatorSqlWorkspaceTest::selectingObjectLoadsMetadataAndSeparateDataThenReturnsToSql() {
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
    auto insert = workspace->adapter()->execute(connection, "INSERT INTO \"Ui table\" VALUES(7)");
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
    QSignalSpy extraReads(workspace->adapter(), &choscordb::EngineAdapter::objectInspectionReady);
    emit workspace->connectionReady(connection);
    QCOMPARE(metadata->model()->rowCount(), 1);
    QCOMPARE(extraReads.count(), 0);
    QTest::mouseClick(panes, Qt::LeftButton, Qt::NoModifier, panes->tabRect(4).center());
    QTRY_COMPARE(data->model()->rowCount(), 1);
    QCOMPARE(data->model()->index(0, 0).data().toString(), QString("7"));
    auto* dataExport = window.findChild<QPushButton*>("objectDataExport");
    auto* refreshObject = window.findChild<QPushButton*>("objectRefresh");
    QTRY_COMPARE(dataExport->mapTo(&window, dataExport->rect().center()).y(),
                 refreshObject->mapTo(&window, refreshObject->rect().center()).y());
    QVERIFY(window.findChild<QPushButton*>("objectRefresh")->isVisible());
    QVERIFY(window.findChild<QPushButton*>("objectRefresh")->isEnabled());
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

void NavigatorSqlWorkspaceTest::objectDataKeepsCancelPaneVisibleUntilAcknowledged() {
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
    QCOMPARE(
        window.findChild<choscordb::ToastRegion*>("toastRegion")->property("variant").toString(),
        QString("warning"));
    QVERIFY(!window.showScreen(choscordb::MainWindow::Screen::Start));
    QSignalSpy changed(explorer, &choscordb::ObjectExplorer::objectChanged);
    explorer->openObject(connection, R"(["main","another"])", "another");
    QCOMPARE(changed.count(), 0);
    QVERIFY(cancel->isHidden());
    auto* objectGrid = explorer->findChild<QTableView*>("objectDataResults");
    QVERIFY(objectGrid);
    QTimer::singleShot(0, objectGrid, [] {
        auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
        QVERIFY(menu);
        QAction* cancelAction = nullptr;
        for (auto* action : menu->actions())
            if (action->text() == QString("Cancel"))
                cancelAction = action;
        QVERIFY(cancelAction);
        QVERIFY(cancelAction->isEnabled());
        menu->close();
        cancelAction->trigger();
    });
    objectGrid->customContextMenuRequested(QPoint(10, 10));
    QTRY_VERIFY(workspace->navigationAllowed());
    QTest::mouseClick(panes, Qt::LeftButton, Qt::NoModifier, panes->tabRect(0).center());
    QCOMPARE(panes->currentIndex(), 0);
    QTRY_COMPARE(explorer->findChild<QTableView*>("objectMetadata")->model()->rowCount(), 1);
}

void NavigatorSqlWorkspaceTest::documentsKeepTargetsAndImmutableResultOrigin() {
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
                QString::fromUtf8(event.state.data(), qsizetype(event.state.size())) == "queued")
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
    QCOMPARE(
        QFileInfo(grid->model()->data(grid->model()->index(0, 0)).toString()).canonicalFilePath(),
        QFileInfo(directory.filePath("first.sqlite")).canonicalFilePath());
    QTRY_VERIFY(run->isEnabled());
    QTRY_VERIFY(
        window.findChild<QPlainTextEdit*>("queryMessages")->toPlainText().contains("Completed in"));
    const auto origin = summary->text();
    QVERIFY(origin.contains(tabs->tabText(0).remove(" •")));
    QVERIFY(origin.contains("first.sqlite"));
    workspace->connectSqlite(directory.filePath("second.sqlite"));
    QTRY_COMPARE(connected.count(), 2);
    QCOMPARE(selector->currentData(), firstConnection);
    first->setText(QString("-- retained line\n").repeated(100));
    first->SendScintilla(QsciScintilla::SCI_APPENDTEXT, static_cast<std::uintptr_t>(8),
                         "-- edit\n");
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
    QTRY_COMPARE(
        QDir::fromNativeSeparators(grid->model()->data(grid->model()->index(0, 0)).toString()),
        QFileInfo(directory.filePath("second.sqlite")).canonicalFilePath());
    QTRY_VERIFY(run->isEnabled());
    QVERIFY(summary->text().contains("second.sqlite"));
    QVERIFY(summary->text() != origin);
    QCOMPARE(queued, 2);
}

void NavigatorSqlWorkspaceTest::activeExecutionKeepsDocumentAndCancelReachable() {
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
    auto* queryOverflow = window.findChild<QToolButton*>("queryToolbarOverflow");
    QVERIFY(queryOverflow);
    QVERIFY(queryOverflow->isVisible());
    QVERIFY(!queryOverflow->menu()->actions().contains(cancel));
    cancel->trigger();
    QCOMPARE(cancel->text(), QString("Cancelling…"));
    QCOMPARE(window.findChild<QLabel*>("executionSummary")->property("state").toString(),
             QString("cancelling"));
    QVERIFY(!run->isEnabled());
    QVERIFY(!cancel->isEnabled());
    QTRY_VERIFY(run->isEnabled());
    tabs->setCurrentWidget(second);
    QCOMPARE(tabs->currentWidget(), second);
}

void NavigatorSqlWorkspaceTest::disconnectMenuKeepsOtherSessionsAndDrafts() {
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
        auto* box =
            qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
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
        auto* box =
            qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
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

void NavigatorSqlWorkspaceTest::generationOpensDraftOnExistingSavedConnectionWithoutExecuting() {
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
            const auto kind = QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
            if (kind == "query_state" &&
                QString::fromUtf8(event.state.data(), qsizetype(event.state.size())) == "queued")
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
    QTRY_VERIFY(
        ddlObject->findChild<QPlainTextEdit*>("objectDdl")->toPlainText().contains("CREATE TABLE"));
    QCOMPARE(choscordb::design::DialogPresentation::activeDialog(), nullptr);
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
    QCOMPARE(window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong(), id);
    QCOMPARE(original->text(), QString("-- preserved draft"));
    menu.findChild<QAction*>("generate_insert")->trigger();
    QCOMPARE(tabs->count(), tabCount + 2);
    auto* insert = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(insert->text().contains("INSERT INTO \"main\".\"a.b\" (\"col name\") VALUES ($1);"));
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
