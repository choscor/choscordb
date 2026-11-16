#include "app/main_window.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "navigator_sql_workspace_test.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QToolBar>
#include <QTreeWidget>
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
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS,
                          static_cast<unsigned long>(editor->text().size() - 3));
    window.findChild<QAction*>("runStatement")->trigger();
    QTRY_COMPARE(cell(), QString("2"));
    QTRY_VERIFY(runButton->isEnabled());
    QVERIFY(!next->isEnabled());
    auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
    runAll->trigger();
    auto* statementTabs = window.findChild<QTabBar*>("sqlStatementResultTabs");
    QVERIFY(statementTabs);
    QTRY_COMPARE(statementTabs->count(), 2);
    QTRY_COMPARE(cell(), QString("2"));
    QTRY_VERIFY(runButton->isEnabled());
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

void NavigatorSqlWorkspaceTest::runAllShowsOneResultTabPerStatement() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* runAll = window.findChild<QAction*>("runAll");
    auto* stop = window.findChild<QAction*>("stopOnFirstError");
    auto* tabs = window.findChild<QTabBar*>("sqlStatementResultTabs");
    auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
    QVERIFY(editor && grid && runAll && stop && tabs && messages);
    QVERIFY(stop->isCheckable() && stop->isChecked());
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(runAll->isEnabled());
    const auto cell = [grid] { return grid->model()->index(0, 0).data().toString(); };
    const auto labels = [tabs] {
        QStringList names;
        for (int index = 0; index < tabs->count(); ++index)
            names << tabs->tabText(index);
        return names;
    };
    editor->setText("-- name: Customers\nSELECT 10 AS a;\nSELECT 20 AS b;\n"
                    "SELECT * FROM missing_table;\nSELECT 40 AS d;");
    runAll->trigger();
    QTRY_COMPARE(labels(), (QStringList{"Customers", "Query 2", "Query 3"}));
    QTRY_VERIFY(runAll->isEnabled());
    QVERIFY(messages->toPlainText().contains("Run all stopped at the failed statement."));
    QVERIFY(tabs->isVisible());
    QCOMPARE(tabs->currentIndex(), 2);
    tabs->setCurrentIndex(0);
    QTRY_COMPARE(cell(), QString("10"));
    QCOMPARE(grid->model()->headerData(0, Qt::Horizontal).toString(), QString("a"));
    tabs->setCurrentIndex(1);
    QTRY_COMPARE(cell(), QString("20"));
    QTRY_VERIFY(stop->isEnabled());
    stop->trigger();
    QTRY_VERIFY(!workspace->queryPreferences().stopOnFirstError);
    runAll->trigger();
    QTRY_COMPARE(labels(), (QStringList{"Customers", "Query 2", "Query 3", "Query 4"}));
    QTRY_VERIFY(runAll->isEnabled());
    QTRY_COMPARE(cell(), QString("40"));
    tabs->setCurrentIndex(0);
    QTRY_COMPARE(cell(), QString("10"));
    struct PinProbe : QObject {
        bool eventFilter(QObject* object, QEvent* event) override {
            if (event->type() == QEvent::Show && object->objectName() == "statementResultMenu")
                if (auto* pin = object->findChild<QAction*>("pinStatementResult"))
                    QTimer::singleShot(0, pin, [pin] {
                        pin->trigger();
                        qobject_cast<QMenu*>(pin->parent())->close();
                    });
            return false;
        }
    } probe;
    qApp->installEventFilter(&probe);
    const auto point = tabs->tabRect(0).center();
    emit tabs->customContextMenuRequested(point);
    qApp->removeEventFilter(&probe);
    QTRY_COMPARE(tabs->tabText(0), QString("Customers · Pinned"));
    editor->setText("SELECT 7 AS g;\nSELECT 8 AS h;");
    runAll->trigger();
    QTRY_COMPARE(labels(), (QStringList{"Customers · Pinned", "Query 1", "Query 2"}));
    QTRY_VERIFY(runAll->isEnabled());
    QTRY_COMPARE(cell(), QString("8"));
    tabs->setCurrentIndex(0);
    QTRY_COMPARE(cell(), QString("10"));
    editor->setText("SELECT 9 AS i;");
    window.findChild<QAction*>("runStatement")->trigger();
    QTRY_COMPARE(labels(), (QStringList{"Customers · Pinned", "Query 1"}));
    QTRY_COMPARE(cell(), QString("9"));
    stop->trigger();
    QTRY_VERIFY(workspace->queryPreferences().stopOnFirstError);
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
    QTRY_VERIFY(source->toolTip().contains("Disconnected. Choose a SQL target."));
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
    QTRY_VERIFY(cancel->isVisible());
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
    emit window.objectContextSelected(alphaId, R"(["main","sqlite_schema"])", "sqlite_schema",
                                      "table");
    QVERIFY(qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget()));
    QVERIFY(!openMenu());
}

void NavigatorSqlWorkspaceTest::explainShowsThePlanWithoutRunningTheStatement() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* runButton = window.findChild<QPushButton*>("runStatementButton");
    auto* explainButton = window.findChild<QPushButton*>("explainStatementButton");
    auto* explain = window.findChild<QAction*>("explainStatement");
    QVERIFY(editor && grid && runButton && explainButton && explain);
    QCOMPARE(explain->shortcut(), QKeySequence("Ctrl+Alt+E"));
    auto* toolbar = window.findChild<QToolBar*>("queryToolbar");
    const auto position = [toolbar](const char* name) {
        return toolbar->findChild<QWidget*>(name)->mapTo(toolbar, QPoint()).x();
    };
    QVERIFY(!explainButton->isEnabled());
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(runButton->isEnabled() && explainButton->isEnabled());
    QVERIFY(position("rowLimit") < position("explainStatementButton"));
    editor->setText("CREATE TABLE t (a INTEGER); CREATE INDEX t_a ON t (a);");
    window.findChild<QAction*>("runAll")->trigger();
    QTRY_VERIFY(runButton->isEnabled());
    editor->setText("INSERT INTO t VALUES (1);");
    runButton->click();
    QTRY_VERIFY(runButton->isEnabled());
    editor->setText("SELECT 1;\nDELETE FROM t WHERE a = 1;");
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS,
                          static_cast<unsigned long>(editor->text().size() - 2));
    explainButton->click();
    auto* sheet = window.findChild<QDialog*>("queryPlanSheet");
    QVERIFY(sheet);
    QTRY_VERIFY(sheet->isVisible());
    auto* tree = sheet->findChild<QTreeWidget*>("queryPlanTree");
    auto* status = sheet->findChild<QLabel*>("queryPlanStatus");
    QTRY_COMPARE(tree->topLevelItemCount(), 1);
    QVERIFY2(tree->topLevelItem(0)->text(0).startsWith("SEARCH t USING INDEX t_a"),
             qPrintable(tree->topLevelItem(0)->text(0)));
    QCOMPARE(status->text(), QString("1 step(s). The statement did not run."));
    sheet->findChild<QPushButton*>("queryPlanClose")->click();
    QTRY_VERIFY(!sheet->isVisible());
    editor->setText("SELECT count(*) FROM t;");
    runButton->click();
    QTRY_COMPARE(grid->model()->index(0, 0).data().toString(), QString("1"));
    editor->setText("SELECT * FROM missing;");
    explain->trigger();
    QTRY_VERIFY(sheet->isVisible());
    QTRY_VERIFY2(status->text().contains("no such table"), qPrintable(status->text()));
    QCOMPARE(tree->topLevelItemCount(), 0);
}
void NavigatorSqlWorkspaceTest::runSqlFileReportsEachFailedStatement() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("load.sql");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("CREATE TABLE t (a INTEGER UNIQUE);\nINSERT INTO t VALUES (1);\n"
                   "INSERT INTO t VALUES (1);\nINSERT INTO t VALUES (2);\n");
    }
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    auto* runButton = window.findChild<QPushButton*>("runStatementButton");
    auto* action = window.findChild<QAction*>("runSqlFile");
    QVERIFY(editor && runButton && action);
    QCOMPARE(action->shortcut(), QKeySequence("Ctrl+Alt+O"));
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(runButton->isEnabled());
    auto* safety = window.findChild<QMenu*>("safetyLevelMenu");
    const auto setSafety = [safety](const char* code) {
        for (auto* level : safety->actions())
            if (level->data() == code)
                level->trigger();
    };
    setSafety("silent");
    action->trigger();
    auto* dialog = window.findChild<QDialog*>("sqlFileDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->isVisible());
    auto* start = dialog->findChild<QPushButton*>("sqlFileStart");
    auto* mode = dialog->findChild<QComboBox*>("sqlFileMode");
    auto* errors = dialog->findChild<QPlainTextEdit*>("sqlFileErrors");
    auto* status =
        dialog->findChild<QWidget*>("sqlFileStatusLine")->findChild<QLabel*>("statusMessage");
    QVERIFY(start && mode && errors && status);
    start->click();
    QTRY_VERIFY2(status->text().contains("Choose a SQL file to run."), qPrintable(status->text()));
    dialog->findChild<QLineEdit*>("sqlFilePath")->setText(path);
    mode->setCurrentIndex(mode->findData("continue"));
    start->click();
    QTRY_VERIFY2(status->text().contains("Ran 3 statement(s). 1 statement(s) failed."),
                 qPrintable(status->text()));
    QVERIFY(errors->isVisible());
    QVERIFY2(errors->toPlainText().startsWith("Line 3: "), qPrintable(errors->toPlainText()));
    QCOMPARE(start->text(), QString("Retry"));
    dialog->findChild<QPushButton*>("sqlFileCancel")->click();
    QTRY_VERIFY(!dialog->isVisible());
    editor->setText("SELECT count(*) FROM t;");
    runButton->click();
    auto* grid = window.findChild<QTableView*>("queryResults");
    QTRY_COMPARE(grid->model()->index(0, 0).data().toString(), QString("2"));
}
