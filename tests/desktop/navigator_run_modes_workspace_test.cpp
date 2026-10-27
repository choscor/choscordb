#include "app/main_window.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "navigator_sql_workspace_test.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>

void NavigatorSqlWorkspaceTest::runModesRunCurrentStatementOrWholeDocument() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* next = window.findChild<QPushButton*>("nextPage");
    auto* runButton = window.findChild<QPushButton*>("runStatementButton");
    auto* runAll = window.findChild<QAction*>("runAll");
    QVERIFY(editor && grid && next && runButton && runAll);
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(runButton->isEnabled());
    const auto cell = [grid] { return grid->model()->index(0, 0).data().toString(); };
    editor->setText("SELECT 1 AS first_value;\nSELECT 2 AS second_value;");
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
    runButton->click();
    QTRY_COMPARE(cell(), QString("1"));
    QTRY_VERIFY(runButton->isEnabled());
    QVERIFY(!next->isEnabled());
    // The cursor in the second statement selects only that statement.
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS,
                          static_cast<unsigned long>(editor->text().size() - 3));
    window.findChild<QAction*>("runStatement")->trigger();
    QTRY_COMPARE(cell(), QString("2"));
    QTRY_VERIFY(runButton->isEnabled());
    QVERIFY(!next->isEnabled());
    // Run all sends the whole document. SQLite rejects more than one statement, so the
    // driver error shows that both statements were sent.
    auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
    runAll->trigger();
    QTRY_VERIFY(messages->toPlainText().contains("Multiple statements provided"));
    QTRY_VERIFY(runButton->isEnabled());
    // A selection overrides Run all.
    editor->setSelection(0, 0, 0, 23);
    runAll->trigger();
    QTRY_COMPARE(cell(), QString("1"));
    QTRY_VERIFY(runButton->isEnabled());
    QVERIFY(!next->isEnabled());
    editor->setText("-- only a comment");
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
    messages->clear();
    runAll->trigger();
    QTRY_VERIFY(messages->toPlainText().contains("No executable statement in the document."));
}

void NavigatorSqlWorkspaceTest::statusLineShowsTheTargetOfEachDocument() {
    QTemporaryDir directory;
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* source = window.findChild<QLabel*>("executionSummary");
    auto* first = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(first && source);
    QTRY_VERIFY(source->toolTip().contains("Disconnected — choose a SQL target"));
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    const auto alpha = directory.filePath("alpha.sqlite");
    const auto beta = directory.filePath("beta.sqlite");
    workspace->connectSqlite(alpha);
    QTRY_COMPARE(connected.count(), 1);
    workspace->connectSqlite(beta);
    QTRY_COMPARE(connected.count(), 2);
    const auto alphaId = connected.at(0).at(0).toULongLong();
    const auto betaId = connected.at(1).at(0).toULongLong();
    QCOMPARE(first->connectionTarget(), std::optional<quint64>(alphaId));
    QTRY_VERIFY(source->toolTip().contains(alpha));
    window.findChild<QAction*>("newQuery")->trigger();
    auto* second = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(second && second != first);
    QCOMPARE(second->connectionTarget(), std::optional<quint64>(alphaId));
    QVERIFY(workspace->changeDocumentConnection(second, betaId));
    QTRY_VERIFY(source->toolTip().contains(beta));
    QVERIFY(source->property("fullSource").toString().contains(beta));
    tabs->setCurrentWidget(first);
    QTRY_VERIFY(source->toolTip().contains(alpha));
    QVERIFY(!source->toolTip().contains(beta));
    tabs->setCurrentWidget(second);
    QTRY_VERIFY(source->toolTip().contains(beta));
}

void NavigatorSqlWorkspaceTest::changeConnectionMenuRebindsOnlyIdleSqlTabs() {
    QTemporaryDir directory;
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(editor);
    const auto openMenu = [&window, tabs]() -> QMenu* {
        if (auto* old = window.findChild<QMenu*>("editorTabContextMenu"))
            old->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* bar = tabs->tabBar();
        const auto point = bar->tabRect(tabs->currentIndex()).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, bar->mapToGlobal(point));
        QApplication::sendEvent(bar, &event);
        return window.findChild<QMenu*>("changeConnectionMenu");
    };
    auto* change = openMenu();
    QVERIFY(change);
    QVERIFY(!change->menuAction()->isEnabled());
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    const auto alpha = directory.filePath("alpha.sqlite");
    const auto beta = directory.filePath("beta.sqlite");
    workspace->connectSqlite(alpha);
    QTRY_COMPARE(connected.count(), 1);
    workspace->connectSqlite(beta);
    QTRY_COMPARE(connected.count(), 2);
    const auto alphaId = connected.at(0).at(0).toULongLong();
    const auto betaId = connected.at(1).at(0).toULongLong();
    QCOMPARE(editor->connectionTarget(), std::optional<quint64>(alphaId));
    change = openMenu();
    QVERIFY(change && change->menuAction()->isEnabled());
    auto actions = change->actions();
    QCOMPARE(actions.size(), 2);
    QCOMPARE(actions.at(0)->text(), alpha);
    QCOMPARE(actions.at(1)->text(), beta);
    QVERIFY(actions.at(0)->isChecked());
    QVERIFY(!actions.at(1)->isChecked());
    actions.at(1)->trigger();
    QCOMPARE(editor->connectionTarget(), std::optional<quint64>(betaId));
    QCOMPARE(editor->targetLabel(), beta);
    auto* run = window.findChild<QAction*>("runStatement");
    auto* cancel = window.findChild<QPushButton*>("cancelQueryButton");
    auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
    QTRY_VERIFY(run->isEnabled());
    editor->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                    "x<100000000) SELECT sum(x) FROM n");
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
    run->trigger();
    QTRY_VERIFY(cancel->isEnabled());
    change = openMenu();
    QVERIFY(change);
    QVERIFY(change->actions().at(1)->isChecked());
    messages->clear();
    change->actions().at(0)->trigger();
    QCOMPARE(editor->connectionTarget(), std::optional<quint64>(betaId));
    QVERIFY(messages->toPlainText().contains(
        "Finish or cancel the active query before changing its connection."));
    cancel->click();
    QTRY_VERIFY(run->isEnabled());
    // Object tabs do not get the submenu.
    emit window.objectContextSelected(alphaId, R"(["main","sqlite_schema"])", "sqlite_schema",
                                      "table");
    QVERIFY(qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget()));
    QVERIFY(!openMenu());
}
