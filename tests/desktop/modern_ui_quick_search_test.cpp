#include "app/appearance_controller.h"
#include "app/editor_preferences.h"
#include "app/main_window.h"
#include "app/navigator_controller.h"
#include "app/object_data_workspace.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/menu/menu.h"
#include "design_system/quick_search/quick_search_dialog.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "modern_ui_test.h"
#include "tools/preview/preview_window.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFontInfo>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QSysInfo>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <Qsci/qscilexersql.h>
#include <Qsci/qsciscintilla.h>
#include <algorithm>

void ModernUiTest::quickSwitchOpensOneSearchOverlayFromViewAction() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    auto* action = window.findChild<QAction*>("quickSwitch");
    QVERIFY(action);
    QCOMPARE(action->shortcut(), QKeySequence("Ctrl+P"));
    action->trigger();
    auto* overlay = window.findChild<QDialog*>("quickSearchDialog");
    QVERIFY(overlay);
    QVERIFY(overlay->isVisible());
    auto* input = overlay->findChild<QLineEdit*>("quickSearchInput");
    QVERIFY(input);
    QTRY_COMPARE(QApplication::focusWidget(), input);
    action->trigger();
    QCOMPARE(window.findChildren<QDialog*>("quickSearchDialog").size(), 1);
    auto* list = overlay->findChild<QListWidget*>("quickSearchResults");
    QVERIFY(list);
    list->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), list);
    QTest::keyClick(list, Qt::Key_P, Qt::ControlModifier);
    QTRY_COMPARE(QApplication::focusWidget(), input);
    overlay->reject();
    QTest::keyClick(&window, Qt::Key_P, Qt::ControlModifier);
    QTRY_VERIFY(overlay->isVisible());
    action->setShortcut(QKeySequence("Ctrl+Shift+P"));
    list->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), list);
    QTest::keyClick(list, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_COMPARE(QApplication::focusWidget(), input);
}

void ModernUiTest::quickSearchEmptyQueryShowsScreensAndOpenTabs() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    auto* action = window.findChild<QAction*>("quickSwitch");
    action->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    QVERIFY(overlay);
    QTRY_COMPARE(overlay->results().size(), 4);
    QStringList types;
    QStringList titles;
    for (const auto& result : overlay->results()) {
        types.append(result.type);
        titles.append(result.title);
        QCOMPARE(result.icon, choscordb::design::Icon::Square);
    }
    QCOMPARE(types, QStringList({"Screen", "Screen", "Screen", "Screen"}));
    QVERIFY(titles.contains("Start page"));
    QVERIFY(titles.contains("SQL workspace"));
    QVERIFY(titles.contains("Object explorer"));
    QVERIFY(titles.contains("Query history"));
    overlay->reject();
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    action->trigger();
    QTRY_VERIFY(overlay->results().size() >= 5);
    bool hasTab = false;
    for (const auto& result : overlay->results()) {
        if (result.type == "Tab") {
            QCOMPARE(result.icon, choscordb::design::Icon::Code);
            hasTab = true;
        }
    }
    QVERIFY(hasTab);
    overlay->reject();
}

void ModernUiTest::quickSearchDistinguishesDuplicateTabTitles() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    window.findChild<QAction*>("newQuery")->trigger();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QCOMPARE(tabs->count(), 2);
    tabs->setTabText(0, "duplicate query");
    tabs->setTabText(1, "duplicate query");
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    overlay->findChild<QLineEdit*>("quickSearchInput")->setText("duplicate query");
    QTRY_COMPARE(overlay->results().size(), 2);
    QCOMPARE(overlay->results().at(0).type, QString("Tab"));
    QCOMPARE(overlay->results().at(1).type, QString("Tab"));
    QVERIFY(overlay->results().at(0).context != overlay->results().at(1).context);
    QVERIFY(overlay->results().at(0).context.contains("Workspace tab 1"));
    QVERIFY(overlay->results().at(1).context.contains("Workspace tab 2"));
}

void ModernUiTest::quickSearchRanksNamesAndActivatesAnOpenTab() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    window.findChild<QAction*>("newQuery")->trigger();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->currentIndex(), 1);
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    auto* input = overlay->findChild<QLineEdit*>("quickSearchInput");
    input->setText("sql work");
    QTRY_VERIFY(!overlay->results().isEmpty());
    QCOMPARE(overlay->results().first().title, QString("SQL workspace"));
    input->setText("Untitled query 1");
    QTRY_VERIFY(!overlay->results().isEmpty());
    QCOMPARE(overlay->results().first().type, QString("Tab"));
    QCOMPARE(overlay->results().first().title, QString("Untitled query 1"));
    QTest::keyClick(input, Qt::Key_Return);
    QTRY_COMPARE(tabs->currentIndex(), 0);
    QVERIFY(!overlay->isVisible());
}

void ModernUiTest::quickSearchFindsSqlInInactiveEditorsAndSelectsTheMatch() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* first = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(first);
    first->setText("SELECT customer_name FROM orders;\nSELECT customer_code FROM orders;");
    window.findChild<QAction*>("newQuery")->trigger();
    auto* second = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(second);
    second->setText("select customer_name from products;");
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    auto* input = overlay->findChild<QLineEdit*>("quickSearchInput");
    input->setText("customer_name");
    QTRY_COMPARE(overlay->results().size(), 2);
    QCOMPARE(overlay->results().first().type, QString("SQL text"));
    QCOMPARE(overlay->results().first().icon, choscordb::design::Icon::Code);
    QVERIFY(overlay->results().first().context.contains("Untitled query 1"));
    QVERIFY(overlay->results().first().context.contains("line 1"));
    QVERIFY(overlay->results().first().title.contains("customer_name"));
    QTest::keyClick(input, Qt::Key_Return);
    QTRY_COMPARE(tabs->currentWidget(), static_cast<QWidget*>(first));
    QCOMPARE(first->selectedText(), QString("customer_name"));
    QCOMPARE(first->text(), QString("SELECT customer_name FROM orders;\n"
                                    "SELECT customer_code FROM orders;"));
    QCOMPARE(second->text(), QString("select customer_name from products;"));
    QVERIFY(!overlay->isVisible());
}

void ModernUiTest::quickSearchShowsLateEditorMatchesInTheirSnippet() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    const QString sql = QString(180, QChar('x')) + QStringLiteral(" late_sql_marker");
    editor->setText(sql);
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    auto* input = overlay->findChild<QLineEdit*>("quickSearchInput");
    input->setText("late_sql_marker");
    QTRY_COMPARE(overlay->results().size(), 1);
    QVERIFY(overlay->results().first().title.contains("late_sql_marker"));
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(editor->selectedText(), QString("late_sql_marker"));
    QCOMPARE(editor->text(), sql);
}

void ModernUiTest::quickSearchClearsEditedSqlResultsBeforeActivation() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    QVERIFY(editor);
    editor->setText("SELECT unique_marker");
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    overlay->findChild<QLineEdit*>("quickSearchInput")->setText("unique_marker");
    QTRY_COMPARE(overlay->results().size(), 1);
    const QString oldId = overlay->results().first().id;
    editor->setText("SELECT a_different_value");
    QTRY_VERIFY(overlay->results().isEmpty());
    emit overlay->activated(oldId);
    QVERIFY(overlay->isVisible());
    QCOMPARE(editor->text(), QString("SELECT a_different_value"));
}

void ModernUiTest::quickSearchClearsResultsWhenAnInactiveTabCloses() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* first = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    first->setText("SELECT obsolete_marker");
    first->setModified(false);
    window.findChild<QAction*>("newQuery")->trigger();
    QCOMPARE(tabs->currentIndex(), 1);
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    overlay->findChild<QLineEdit*>("quickSearchInput")->setText("obsolete_marker");
    QTRY_COMPARE(overlay->results().size(), 1);
    const QString oldId = overlay->results().first().id;
    emit tabs->tabCloseRequested(0);
    QTRY_COMPARE(tabs->count(), 1);
    QTRY_VERIFY(overlay->results().isEmpty());
    emit overlay->activated(oldId);
    QVERIFY(overlay->isVisible());
    QCOMPARE(tabs->count(), 1);
}

void ModernUiTest::quickSearchFindsSavedHistoryAndOpensItWithoutExecuting() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(connected.count(), 1);
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    const QString savedSql = QStringLiteral("SELECT '") + QString(180, QChar('x')) +
                             QStringLiteral("kept_history_marker' AS value");
    editor->setText(savedSql);
    int finished = 0;
    connect(workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&finished](const choscordb::BridgeEvent& event) {
                if (QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size())) ==
                    QStringLiteral("query_finished"))
                    ++finished;
            });
    QTRY_VERIFY(window.findChild<QAction*>("runStatement")->isEnabled());
    window.findChild<QAction*>("runStatement")->trigger();
    QTRY_COMPARE(finished, 1);
    QTRY_VERIFY(workspace->navigationAllowed());
    QSignalSpy listed(workspace->adapter(), &choscordb::EngineAdapter::historyListed);
    QVERIFY(workspace->adapter()->listHistory(50, 0, 8842));
    QTRY_COMPARE(listed.count(), 1);
    const auto saved = qvariant_cast<QList<choscordb::SavedHistoryEntry>>(listed.first().at(1));
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.first().sql, savedSql);
    editor->setText("SELECT 1");
    int started = 0;
    connect(workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&started](const choscordb::BridgeEvent& event) {
                if (QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size())) ==
                    QStringLiteral("query_started"))
                    ++started;
            });
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    auto* input = overlay->findChild<QLineEdit*>("quickSearchInput");
    input->setText("kept_history_marker");
    QTRY_COMPARE(overlay->results().size(), 1);
    QCOMPARE(overlay->results().first().type, QString("History"));
    QCOMPARE(overlay->results().first().icon, choscordb::design::Icon::Refresh);
    QVERIFY(overlay->results().first().title.contains("kept_history_marker"));
    QTest::keyClick(input, Qt::Key_Return);
    QTRY_COMPARE(tabs->count(), 2);
    auto* reopened = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(reopened);
    QCOMPARE(reopened->text(), saved.first().sql);
    QCOMPARE(reopened->property("historyRecordId").toString(), saved.first().id);
    QVERIFY(!overlay->isVisible());
    QCOMPARE(started, 0);
    QSignalSpy policySaved(workspace->adapter(), &choscordb::EngineAdapter::historyPolicyReady);
    QVERIFY(workspace->adapter()->setHistoryPolicy({false, 90, 10000}, 8843));
    QTRY_VERIFY(!policySaved.isEmpty());
    window.findChild<QAction*>("quickSwitch")->trigger();
    input->setText("kept_history_marker");
    QTRY_VERIFY([overlay] {
        const auto rows = overlay->results();
        return std::any_of(rows.cbegin(), rows.cend(),
                           [](const auto& result) { return result.type == "History"; });
    }());
    QTRY_VERIFY(overlay->findChild<QLabel*>("quickSearchStatus")
                    ->text()
                    .contains("History recording is off"));
    QString oldHistoryId;
    for (const auto& result : overlay->results())
        if (result.type == "History")
            oldHistoryId = result.id;
    QVERIFY(!oldHistoryId.isEmpty());
    QSignalSpy cleared(workspace->adapter(), &choscordb::EngineAdapter::historyCleared);
    QVERIFY(workspace->adapter()->clearHistory(8844));
    QVERIFY(overlay->findChild<QLabel*>("quickSearchStatus")
                ->text()
                .contains("History is being cleared"));
    emit overlay->activated(oldHistoryId);
    QVERIFY(overlay->isVisible());
    QTRY_VERIFY(!cleared.isEmpty());
    QTRY_VERIFY([overlay] {
        const auto rows = overlay->results();
        return std::none_of(rows.cbegin(), rows.cend(),
                            [](const auto& result) { return result.type == "History"; });
    }());
    emit overlay->activated(oldHistoryId);
    QVERIFY(overlay->isVisible());
}

void ModernUiTest::quickSearchShowsHistoryClearStartedBeforeFirstOpen() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    auto* adapter = window.findChild<choscordb::QueryWorkspace*>()->adapter();
    QSignalSpy cleared(adapter, &choscordb::EngineAdapter::historyCleared);
    QVERIFY(adapter->clearHistory(8845));
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    QVERIFY(overlay);
    QVERIFY(overlay->findChild<QLabel*>("quickSearchStatus")
                ->text()
                .contains("History is being cleared"));
    QTRY_COMPARE(cleared.count(), 1);

    overlay->reject();
    QVERIFY(adapter->clearHistory(8846));
    window.findChild<QAction*>("quickSwitch")->trigger();
    QVERIFY(overlay->findChild<QLabel*>("quickSearchStatus")
                ->text()
                .contains("History is being cleared"));
    QTRY_COMPARE(cleared.count(), 2);

    QTemporaryDir secondStorage;
    choscordb::MainWindow secondWindow(nullptr, secondStorage.filePath("settings.sqlite"));
    secondWindow.show();
    QTRY_VERIFY(secondWindow.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    auto* secondAdapter = secondWindow.findChild<choscordb::QueryWorkspace*>()->adapter();
    QSignalSpy secondCleared(secondAdapter, &choscordb::EngineAdapter::historyCleared);
    QVERIFY(secondAdapter->clearHistory(8847));
    secondWindow.findChild<QAction*>("quickSwitch")->trigger();
    auto* secondOverlay =
        secondWindow.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    QVERIFY(secondOverlay);
    QVERIFY(secondOverlay->findChild<QLabel*>("quickSearchStatus")
                ->text()
                .contains("History is being cleared"));
    emit secondAdapter->recoveryFailed(8847, QStringLiteral("simulated failure"));
    QVERIFY(secondOverlay->findChild<QLabel*>("quickSearchStatus")
                ->text()
                .contains("History clear failed"));
    QTRY_COMPARE(secondCleared.count(), 1);
}

void ModernUiTest::quickSearchFindsCollapsedSelectedConnectionObjectBeforeSqlText() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(connected.count(), 1);
    const quint64 connection = connected.first().first().toULongLong();
    auto* navigator = window.findChild<choscordb::NavigatorController*>();
    navigator->setSelectedConnection(connection);
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    editor->setText("CREATE TABLE invoice_lines (id INTEGER)");
    int finished = 0;
    connect(workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&finished](const choscordb::BridgeEvent& event) {
                if (QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size())) ==
                    QStringLiteral("query_finished"))
                    ++finished;
            });
    QTRY_VERIFY(window.findChild<QAction*>("runStatement")->isEnabled());
    window.findChild<QAction*>("runStatement")->trigger();
    QTRY_COMPARE(finished, 1);
    QTRY_VERIFY(workspace->navigationAllowed());
    auto* filter = window.findChild<QLineEdit*>("navigatorFilter");
    QVERIFY(filter);
    filter->setText("sidebar_only");
    auto* tree = window.findChild<QTreeView*>("databaseNavigator");
    QVERIFY(tree);
    QVERIFY(!tree->isExpanded(tree->model()->index(0, 0)));
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    auto* input = overlay->findChild<QLineEdit*>("quickSearchInput");
    input->setText("invoice lin");
    QTRY_VERIFY2(!overlay->results().isEmpty() && overlay->results().first().type == "Object",
                 qPrintable(overlay->findChild<QLabel*>("quickSearchStatus")->text()));
    QVERIFY(overlay->results().first().title.contains("invoice_lines"));
    QVERIFY(overlay->results().first().context.contains("table"));
    QCOMPARE(filter->text(), QString("sidebar_only"));
    const QString staleObjectId = overlay->results().first().id;
    navigator->clearSelectedConnection();
    QTRY_VERIFY(overlay->results().isEmpty());
    emit overlay->activated(staleObjectId);
    QVERIFY(overlay->isVisible());
    navigator->setSelectedConnection(connection);
    QTRY_VERIFY2(!overlay->results().isEmpty() && overlay->results().first().type == "Object",
                 qPrintable(overlay->findChild<QLabel*>("quickSearchStatus")->text()));
    overlay->findChild<QListWidget*>("quickSearchResults")->setCurrentRow(0);
    QTest::keyClick(input, Qt::Key_Return);
    auto* object = qobject_cast<choscordb::ObjectExplorer*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    QVERIFY(object);
    QVERIFY(object->property("objectLabel").toString().contains("invoice_lines"));
    QCOMPARE(navigator->selectedConnection(), connection);
    QVERIFY(!overlay->isVisible());
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    tabs->setCurrentIndex(0);
    auto* sqlTab = tabs->currentWidget();
    window.findChild<QAction*>("quickSwitch")->trigger();
    QString recentObjectId;
    for (const auto& result : overlay->results())
        if (result.type == "Object" && result.title.contains("invoice_lines") &&
            result.context.contains("Recent"))
            recentObjectId = result.id;
    QStringList visibleRows;
    for (const auto& result : overlay->results())
        visibleRows << QStringLiteral("%1|%2|%3").arg(result.type, result.title, result.context);
    QVERIFY2(!recentObjectId.isEmpty(), qPrintable(visibleRows.join(QStringLiteral("; "))));
    QVERIFY(navigator->model()->removeConnection(connection));
    emit overlay->activated(recentObjectId);
    QVERIFY(overlay->isVisible());
    QCOMPARE(tabs->currentWidget(), sqlTab);
    overlay->reject();
    QTRY_VERIFY(workspace->navigationAllowed());
    auto* linkedData = object->findChild<choscordb::ObjectDataWorkspace*>();
    QVERIFY(linkedData);
    emit linkedData->foreignKeyRequested(connection, QStringLiteral("linked-unloaded"),
                                         QStringLiteral("linked_unloaded"),
                                         QStringLiteral("id = 1"));
    QTRY_COMPARE(tabs->count(), 3);
    auto* firstLinkedTab = tabs->currentWidget();
    QTRY_VERIFY(workspace->navigationAllowed());
    emit linkedData->foreignKeyRequested(connection, QStringLiteral("linked-unloaded"),
                                         QStringLiteral("linked_unloaded"),
                                         QStringLiteral("id = 2"));
    QTRY_COMPARE(tabs->count(), 4);
    auto* linkedTab = tabs->currentWidget();
    QVERIFY(linkedTab != firstLinkedTab);
    QTRY_VERIFY(workspace->navigationAllowed());
    window.findChild<QAction*>("quickSwitch")->trigger();
    QString linkedRecentId;
    for (const auto& result : overlay->results())
        if (result.type == "Object" && result.title == "linked_unloaded" &&
            result.context.contains("Recent")) {
            linkedRecentId = result.id;
            break;
        }
    QVERIFY(!linkedRecentId.isEmpty());
    tabs->setCurrentWidget(sqlTab);
    emit overlay->activated(linkedRecentId);
    QCOMPARE(tabs->currentWidget(), linkedTab);
    QVERIFY(!overlay->isVisible());
    emit tabs->tabCloseRequested(tabs->indexOf(linkedTab));
    QTRY_COMPARE(tabs->count(), 3);
    QCoreApplication::processEvents();
    window.findChild<QAction*>("quickSwitch")->trigger();
    tabs->setCurrentWidget(sqlTab);
    linkedRecentId.clear();
    for (const auto& result : overlay->results())
        if (result.type == "Object" && result.title == "linked_unloaded") {
            linkedRecentId = result.id;
            break;
        }
    QVERIFY(!linkedRecentId.isEmpty());
    emit overlay->activated(linkedRecentId);
    QTRY_VERIFY2(
        overlay->findChild<QLabel*>("quickSearchStatus")->text().contains("no longer available"),
        qPrintable(overlay->findChild<QLabel*>("quickSearchStatus")->text()));
    QVERIFY(overlay->isVisible());
    QCOMPARE(tabs->currentWidget(), sqlTab);
}

void ModernUiTest::quickSearchHidesRecentSystemObjectAfterPreferenceChanges() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(connected.count(), 1);
    const quint64 connection = connected.first().first().toULongLong();
    auto* navigator = window.findChild<choscordb::NavigatorController*>();
    navigator->setSelectedConnection(connection);
    navigator->setDriverResolver([](quint64) { return QStringLiteral("postgres"); });
    emit window.findChild<choscordb::EditorPreferencesController*>()->systemSchemaVisibilitySaved(
        true);
    auto* model = navigator->model();
    QObject::disconnect(model, &choscordb::NavigatorModel::childrenRequested, workspace->adapter(),
                        &choscordb::EngineAdapter::loadMetadata);
    QSignalSpy requested(model, &choscordb::NavigatorModel::childrenRequested);
    const auto root = model->index(0, 0);
    model->fetchMore(root);
    QTRY_COMPARE(requested.count(), 1);
    QVERIFY(model->applyChildren(connection, {}, requested.last().at(2).toULongLong(),
                                 {{"catalog", "pg_catalog", "pg_catalog", "schema", true},
                                  {"public", "public", "public", "schema", true}}));
    const auto schema = model->index(0, 0, root);
    model->fetchMore(schema);
    QTRY_COMPARE(requested.count(), 2);
    QVERIFY(model->applyChildren(
        connection, "catalog", requested.last().at(2).toULongLong(),
        {{"catalog-table", "catalog_table", "pg_catalog.catalog_table", "table", false}}));
    emit window.objectContextSelected(connection, "catalog-table", "pg_catalog.catalog_table",
                                      "table");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QTRY_COMPARE(tabs->count(), 2);
    QTRY_VERIFY(workspace->navigationAllowed());
    auto* object = qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget());
    QVERIFY(object);
    auto* linkedData = object->findChild<choscordb::ObjectDataWorkspace*>();
    QVERIFY(linkedData);
    emit linkedData->foreignKeyRequested(connection, QStringLiteral("unloaded-system"),
                                         QStringLiteral("pg_catalog.unloaded_system"),
                                         QStringLiteral("id = 1"));
    QTRY_COMPARE(tabs->count(), 3);
    QTRY_VERIFY(workspace->navigationAllowed());
    emit linkedData->foreignKeyRequested(connection, QStringLiteral("unloaded-normal"),
                                         QStringLiteral("\"public\".\"unloaded_normal\""),
                                         QStringLiteral("id = 2"));
    QTRY_COMPARE(tabs->count(), 4);
    QTRY_VERIFY(workspace->navigationAllowed());
    emit tabs->tabCloseRequested(tabs->currentIndex());
    QTRY_COMPARE(tabs->count(), 3);
    emit tabs->tabCloseRequested(tabs->currentIndex());
    QTRY_COMPARE(tabs->count(), 2);
    tabs->setCurrentWidget(object);
    emit tabs->tabCloseRequested(tabs->currentIndex());
    QTRY_COMPARE(tabs->count(), 1);

    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    QTRY_VERIFY([overlay] {
        const auto rows = overlay->results();
        return std::any_of(rows.cbegin(), rows.cend(), [](const auto& result) {
            return result.type == "Object" && result.title == "catalog_table";
        });
    }());
    QString recentId;
    for (const auto& result : overlay->results())
        if (result.type == "Object" && result.title == "catalog_table")
            recentId = result.id;
    QVERIFY(!recentId.isEmpty());
    const auto recentRows = overlay->results();
    QVERIFY(std::any_of(recentRows.cbegin(), recentRows.cend(), [](const auto& result) {
        return result.type == "Object" && result.title == "pg_catalog.unloaded_system";
    }));
    QVERIFY(std::any_of(recentRows.cbegin(), recentRows.cend(), [](const auto& result) {
        return result.type == "Object" && result.title == "\"public\".\"unloaded_normal\"";
    }));

    emit window.findChild<choscordb::EditorPreferencesController*>()->systemSchemaVisibilitySaved(
        false);
    QTRY_VERIFY([overlay] {
        const auto rows = overlay->results();
        return std::none_of(rows.cbegin(), rows.cend(), [](const auto& result) {
            return result.type == "Object" && (result.title == "catalog_table" ||
                                               result.title == "pg_catalog.unloaded_system");
        });
    }());
    const auto visibleRows = overlay->results();
    QVERIFY(std::any_of(visibleRows.cbegin(), visibleRows.cend(), [](const auto& result) {
        return result.type == "Object" && result.title == "\"public\".\"unloaded_normal\"";
    }));
    emit overlay->activated(recentId);
    QVERIFY(overlay->isVisible());
    QCOMPARE(tabs->count(), 1);
    QString ordinaryRecentId;
    for (const auto& result : overlay->results())
        if (result.type == "Object" && result.title == "\"public\".\"unloaded_normal\"")
            ordinaryRecentId = result.id;
    QVERIFY(!ordinaryRecentId.isEmpty());
    emit overlay->activated(ordinaryRecentId);
    QTRY_COMPARE(requested.count(), 3);
    QCOMPARE(requested.last().at(1).toString(), QString("public"));
    // A queued UI refresh must not cancel verification of an activated recent
    // object while its metadata request is still outstanding.
    emit navigator->browsingVisibilityChanged();
    QVERIFY(model->applyChildren(connection, "public", requested.last().at(2).toULongLong(),
                                 {{"unloaded-normal", "unloaded_normal",
                                   "\"public\".\"unloaded_normal\"", "table", false}}));
    QTRY_COMPARE(tabs->count(), 2);
    QVERIFY(!overlay->isVisible());
}

void ModernUiTest::quickSearchColumnResultOpensItsTablePane() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(connected.count(), 1);
    window.findChild<choscordb::NavigatorController*>()->setSelectedConnection(
        connected.first().first().toULongLong());
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    editor->setText("CREATE TABLE invoices (id INTEGER, distinct_field TEXT)");
    int finished = 0;
    connect(workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&finished](const choscordb::BridgeEvent& event) {
                if (QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size())) ==
                    QStringLiteral("query_finished"))
                    ++finished;
            });
    QTRY_VERIFY(window.findChild<QAction*>("runStatement")->isEnabled());
    window.findChild<QAction*>("runStatement")->trigger();
    QTRY_COMPARE(finished, 1);
    QTRY_VERIFY(workspace->navigationAllowed());
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    auto* input = overlay->findChild<QLineEdit*>("quickSearchInput");
    input->setText("distinct_field");
    QTRY_VERIFY2(!overlay->results().isEmpty() && overlay->results().first().type == "Object",
                 qPrintable(overlay->findChild<QLabel*>("quickSearchStatus")->text()));
    QCOMPARE(overlay->results().first().icon, choscordb::design::Icon::File);
    QVERIFY(overlay->results().first().context.contains("column"));
    overlay->findChild<QListWidget*>("quickSearchResults")->setCurrentRow(0);
    QTest::keyClick(input, Qt::Key_Return);
    auto* object = qobject_cast<choscordb::ObjectExplorer*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    QVERIFY(object);
    QVERIFY(object->property("objectLabel").toString().contains("invoices"));
    QCOMPARE(object->paneIndex(), 0);
    QVERIFY(!overlay->isVisible());
}

void ModernUiTest::quickSearchRecoveryGuardKeepsTheCurrentWorkspace() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QTRY_VERIFY(recovery->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* original = tabs->currentWidget();
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    QVERIFY(overlay && overlay->isVisible());
    recovery->requestClose();
    QVERIFY(recovery->isClosing());
    emit overlay->activated(QStringLiteral("showObjects"));
    QVERIFY(overlay->isVisible());
    QCOMPARE(tabs->currentWidget(), original);
    QVERIFY(
        overlay->findChild<QLabel*>("quickSearchStatus")->text().contains("recovery or closing"));
    recovery->cancelClose();
    emit overlay->activated(QStringLiteral("showStart"));
    QVERIFY(overlay->isVisible());
    QCOMPARE(tabs->currentWidget(), original);
    QVERIFY(overlay->findChild<QLabel*>("quickSearchStatus")
                ->text()
                .contains("Close all workspace tabs"));
    overlay->reject();
}

void ModernUiTest::quickSearchActiveQueryGuardKeepsTheCurrentTab() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* first = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(first);
    window.findChild<QAction*>("newQuery")->trigger();
    QCOMPARE(tabs->count(), 2);
    tabs->setCurrentWidget(first);
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    workspace->connectSqlite(":memory:");
    auto* run = window.findChild<QAction*>("runStatement");
    QTRY_VERIFY(run->isEnabled());
    first->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                   "x<100000000) SELECT sum(x) FROM n");
    run->trigger();
    QTRY_VERIFY(!workspace->navigationAllowed());
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    QVERIFY(overlay && overlay->isVisible());
    emit overlay->activated(QStringLiteral("tab:1"));
    QVERIFY(overlay->isVisible());
    QCOMPARE(tabs->currentWidget(), static_cast<QWidget*>(first));
    QVERIFY(
        overlay->findChild<QLabel*>("quickSearchStatus")->text().contains("active database work"));
    window.findChild<QAction*>("command_cancel_query")->trigger();
    QTRY_VERIFY(workspace->navigationAllowed());
    overlay->reject();
}

void ModernUiTest::quickSearchUsesTheBrowsedConnectionWhenTwoAreVisible() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(connected.count(), 1);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(connected.count(), 2);
    const quint64 firstConnection = connected.at(0).at(0).toULongLong();
    const quint64 secondConnection = connected.at(1).at(0).toULongLong();
    QVERIFY(firstConnection != secondConnection);
    auto* navigator = window.findChild<choscordb::NavigatorController*>();
    navigator->setVisibleConnections({firstConnection, secondConnection});
    auto* tree = window.findChild<QTreeView*>("databaseNavigator");
    QVERIFY(tree);
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    editor->setConnectionTarget(secondConnection);
    editor->setText("CREATE TABLE only_second_connection (id INTEGER)");
    int finished = 0;
    connect(workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&finished](const choscordb::BridgeEvent& event) {
                if (QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size())) ==
                    QStringLiteral("query_finished"))
                    ++finished;
            });
    QTRY_VERIFY(window.findChild<QAction*>("runStatement")->isEnabled());
    window.findChild<QAction*>("runStatement")->trigger();
    QTRY_COMPARE(finished, 1);
    QTRY_COMPARE(tree->model()->rowCount(), 2);
    tree->selectionModel()->setCurrentIndex(tree->model()->index(1, 0),
                                            QItemSelectionModel::ClearAndSelect |
                                                QItemSelectionModel::Rows);
    QTRY_COMPARE(window.browsingConnection(), std::optional<quint64>{secondConnection});
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    overlay->findChild<QLineEdit*>("quickSearchInput")->setText("only_second_connection");
    QTRY_VERIFY2(!overlay->results().isEmpty() && overlay->results().first().type == "Object",
                 qPrintable(overlay->findChild<QLabel*>("quickSearchStatus")->text()));
    const QString staleId = overlay->results().first().id;
    tree->selectionModel()->setCurrentIndex(tree->model()->index(0, 0),
                                            QItemSelectionModel::ClearAndSelect |
                                                QItemSelectionModel::Rows);
    QTRY_COMPARE(window.browsingConnection(), std::optional<quint64>{firstConnection});
    QTRY_VERIFY(overlay->results().isEmpty());
    emit overlay->activated(staleId);
    QVERIFY(overlay->isVisible());
}

void ModernUiTest::quickSearchDoesNotReuseAnObjectIdAfterMetadataRefresh() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(connected.count(), 1);
    const quint64 connection = connected.first().first().toULongLong();
    auto* navigator = window.findChild<choscordb::NavigatorController*>();
    navigator->setSelectedConnection(connection);
    auto* model = navigator->model();
    QObject::disconnect(model, &choscordb::NavigatorModel::childrenRequested, workspace->adapter(),
                        &choscordb::EngineAdapter::loadMetadata);
    QSignalSpy requested(model, &choscordb::NavigatorModel::childrenRequested);
    window.findChild<QAction*>("quickSwitch")->trigger();
    auto* overlay = window.findChild<choscordb::design::QuickSearchDialog*>("quickSearchDialog");
    overlay->findChild<QLineEdit*>("quickSearchInput")->setText("shared_name");
    QTRY_COMPARE(requested.count(), 1);
    QVERIFY(model->applyChildren(connection, {}, requested.last().at(2).toULongLong(),
                                 {{"old", "shared_name_old", "shared_name_old", "table", false}}));
    QTRY_VERIFY2(!overlay->results().isEmpty() && overlay->results().first().type == "Object",
                 qPrintable(overlay->findChild<QLabel*>("quickSearchStatus")->text()));
    const QString oldId = overlay->results().first().id;
    model->refresh(model->index(0, 0));
    QTRY_COMPARE(requested.count(), 2);
    QVERIFY(model->applyChildren(connection, {}, requested.last().at(2).toULongLong(),
                                 {{"new", "shared_name_new", "shared_name_new", "table", false}}));
    QTRY_VERIFY(!overlay->results().isEmpty() &&
                overlay->results().first().title == "shared_name_new");
    QVERIFY(overlay->results().first().id != oldId);
    emit overlay->activated(oldId);
    QVERIFY(overlay->isVisible());
    QCOMPARE(window.findChild<QTabWidget*>("editorTabs")->count(), 1);
    overlay->reject();
    emit window.objectContextSelected(connection, "new", "shared_name_new", "table");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QTRY_COMPARE(tabs->count(), 2);
    QTRY_VERIFY(workspace->navigationAllowed());
    emit tabs->tabCloseRequested(tabs->currentIndex());
    QTRY_COMPARE(tabs->count(), 1);
    window.findChild<QAction*>("quickSwitch")->trigger();
    const auto recentRows = overlay->results();
    QVERIFY(std::any_of(recentRows.cbegin(), recentRows.cend(), [](const auto& row) {
        return row.type == "Object" && row.title == "shared_name_new";
    }));
    model->refresh(model->index(0, 0));
    QTRY_COMPARE(requested.count(), 3);
    QVERIFY(model->applyChildren(connection, {}, requested.last().at(2).toULongLong(),
                                 {{"new", "replacement", "replacement", "view", false}}));
    QTRY_VERIFY([overlay] {
        const auto rows = overlay->results();
        return std::none_of(rows.cbegin(), rows.cend(), [](const auto& row) {
            return row.type == "Object" && row.title == "shared_name_new";
        });
    }());
}
