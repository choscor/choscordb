#include "app/appearance_controller.h"
#include "app/main_window.h"
#include "app/query_workspace.h"
#include "design_system/quick_search/quick_search_dialog.h"
#include "widgets/database_action_dialog/database_action_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest>

class SqlTextCommandsTest : public QObject {
    Q_OBJECT
    static choscordb::SqlEditor* openEditor(choscordb::MainWindow& window) {
        window.show();
        if (!QTest::qWaitFor([&] {
                return window.findChild<choscordb::AppearanceController*>()->isReady() &&
                       window.findChild<QAction*>("newQuery")->isEnabled();
            }))
            return nullptr;
        window.findChild<QAction*>("newQuery")->trigger();
        return qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    }

  private slots:
    void switchDatabaseSwitchesTheConnectionInPlace() {
        QTemporaryDir folder;
        for (const auto* name : {"main.sqlite", "archive.db", "notes.txt"}) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        choscordb::MainWindow window(nullptr, folder.filePath("settings.store"));
        auto* editor = openEditor(window);
        QVERIFY(editor);
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        auto* command = window.findChild<QAction*>("command_switch_database");
        QVERIFY(command);
        QCOMPARE(command->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_K));
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        workspace->connectSqlite(folder.filePath("main.sqlite"));
        QTRY_COMPARE(connected.count(), 1);
        const auto first = connected.first().at(0).toULongLong();
        QTRY_COMPARE(workspace->selectedConnection(), std::optional<quint64>(first));
        command->trigger();
        auto* picker = window.findChild<choscordb::design::QuickSearchDialog*>("databasePicker");
        QVERIFY(picker);
        QTRY_VERIFY(picker->isVisible());
        QTRY_COMPARE(picker->results().size(), 2);
        QCOMPARE(picker->results().at(0).title, QString("archive.db"));
        QCOMPARE(picker->results().at(1).title, QString("main.sqlite"));
        QCOMPARE(picker->results().at(1).context, QString("Current"));
        auto* input = picker->findChild<QLineEdit*>();
        QVERIFY(input);
        input->setText("arch");
        QTRY_COMPARE(picker->results().size(), 1);
        const auto target = picker->results().first().id;
        QSignalSpy switched(workspace, &choscordb::QueryWorkspace::databaseSwitched);
        emit picker->activated(target);
        QTRY_COMPARE(switched.count(), 1);
        QCOMPARE(switched.first().at(0).toULongLong(), first);
        QCOMPARE(connected.count(), 1);
        QCOMPARE(workspace->selectedConnection(), std::optional<quint64>(first));
        QCOMPARE(workspace->connectionSource(first)->path, target);
        QCOMPARE(workspace->liveConnectionLabel(first), std::optional<QString>(target));
        QVERIFY(target.endsWith("archive.db"));
        command->trigger();
        QTRY_VERIFY(picker->isVisible());
        QTRY_COMPARE(picker->results().size(), 2);
        QCOMPARE(picker->results().at(0).context, QString("Current"));
    }
    void aFailedSwitchKeepsTheCurrentDatabase() {
        QTemporaryDir folder;
        choscordb::MainWindow window(nullptr, folder.filePath("settings.store"));
        QVERIFY(openEditor(window));
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        workspace->connectSqlite(folder.filePath("main.sqlite"));
        QTRY_COMPARE(connected.count(), 1);
        const auto first = connected.first().at(0).toULongLong();
        choscordb::SavedProfile missing;
        missing.driver = "sqlite";
        missing.path = folder.filePath("missing/other.sqlite");
        QVERIFY(workspace->switchDatabase(first, missing));
        QVERIFY(!workspace->switchDatabase(first, missing));
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Could not switch the database"));
        QCOMPARE(workspace->connectionSource(first)->path, folder.filePath("main.sqlite"));
        QVERIFY(workspace->switchDatabase(first, missing));
    }
    void theToolbarDatabaseButtonOpensTheSwitcher() {
        QTemporaryDir folder;
        for (const auto* name : {"main.sqlite", "archive.db"}) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        choscordb::MainWindow window(nullptr, folder.filePath("settings.store"));
        QVERIFY(openEditor(window));
        auto* toolbar = window.findChild<QToolBar*>("queryToolbar");
        auto* button = toolbar->findChild<QPushButton*>("switchDatabaseButton");
        QVERIFY(button && button->isVisible());
        const auto position = [toolbar](const char* name) {
            return toolbar->findChild<QWidget*>(name)->mapTo(toolbar, QPoint()).x();
        };
        QVERIFY(position("safetyLevel") < position("switchDatabaseButton"));
        QVERIFY(position("switchDatabaseButton") < position("rowLimit"));
        QVERIFY(button->toolTip().contains(
            QKeySequence(Qt::CTRL | Qt::Key_K).toString(QKeySequence::NativeText)));
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        workspace->connectSqlite(folder.filePath("main.sqlite"));
        QTRY_COMPARE(connected.count(), 1);
        button->click();
        auto* picker = window.findChild<choscordb::design::QuickSearchDialog*>("databasePicker");
        QVERIFY(picker);
        QTRY_VERIFY(picker->isVisible());
        QTRY_COMPARE(picker->results().size(), 2);
    }
    void manageDatabasesShowsTheReasonForSqliteFiles() {
        QTemporaryDir folder;
        QFile file(folder.filePath("main.sqlite"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        choscordb::MainWindow window(nullptr, folder.filePath("settings.store"));
        QVERIFY(openEditor(window));
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        workspace->connectSqlite(folder.filePath("main.sqlite"));
        QTRY_COMPARE(connected.count(), 1);
        QTRY_VERIFY(workspace->selectedConnection().has_value());
        auto* command = window.findChild<QAction*>("command_manage_databases");
        QVERIFY(command);
        QCOMPARE(command->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D));
        command->trigger();
        choscordb::DatabaseActionDialog* dialog = nullptr;
        QTRY_VERIFY((dialog = window.findChild<choscordb::DatabaseActionDialog*>()) &&
                    dialog->isVisible());
        dialog->findChild<QLineEdit*>("databaseActionName")->setText("other");
        auto* error = dialog->findChild<QLabel*>("databaseActionError");
        QCOMPARE(error->text(), QString("SQLite keeps each database in its own file. Create, "
                                        "rename or delete the file instead."));
        QVERIFY(error->isVisibleTo(dialog));
        QVERIFY(!dialog->findChild<QPushButton*>("databaseActionApply")->isEnabled());
        dialog->findChild<QPushButton*>("databaseActionCancel")->click();
    }
    void databaseActionsPreviewTheSqlAndNeedTheTypedNameToDrop() {
        choscordb::EngineAdapter adapter;
        choscordb::DatabaseList databases;
        databases.choices = {{"app", "app"}, {"old", "old"}};
        databases.current = "app";
        choscordb::DatabaseActionDialog dialog(&adapter, 1, "postgres", databases);
        dialog.show();
        auto* kind = dialog.findChild<QComboBox*>("databaseActionKind");
        auto* sql = dialog.findChild<QPlainTextEdit*>("databaseActionSql");
        auto* apply = dialog.findChild<QPushButton*>("databaseActionApply");
        auto* name = dialog.findChild<QLineEdit*>("databaseActionName");
        QVERIFY(name->isVisibleTo(&dialog));
        name->setText("sales");
        dialog.findChild<QLineEdit*>("databaseActionEncoding")->setText("UTF8");
        QCOMPARE(sql->toPlainText(),
                 QString("CREATE DATABASE \"sales\" TEMPLATE template0 ENCODING 'UTF8';"));
        QVERIFY(apply->isEnabled());
        kind->setCurrentIndex(kind->findData("drop"));
        auto* target = dialog.findChild<QComboBox*>("databaseActionTarget");
        QVERIFY(target->isVisibleTo(&dialog) && !name->isVisibleTo(&dialog));
        QCOMPARE(target->count(), 1);
        QCOMPARE(target->currentText(), QString("old"));
        QVERIFY(!apply->isEnabled());
        QCOMPARE(dialog.findChild<QLabel*>("databaseActionError")->text(),
                 QString("Type the database name old to drop it."));
        dialog.findChild<QLineEdit*>("databaseActionConfirm")->setText("old");
        QCOMPARE(sql->toPlainText(), QString("DROP DATABASE \"old\";"));
        QVERIFY(apply->isEnabled());
    }
    void formatAndMinifyReplaceTheStatementAtTheCursor() {
        choscordb::MainWindow window;
        auto* editor = openEditor(window);
        QVERIFY(editor);
        editor->setText("select 1;\nselect a, b from t where x = 1");
        editor->setCursorPosition(1, 3);
        auto* format = window.findChild<QAction*>("command_format_sql");
        QVERIFY(format);
        QCOMPARE(format->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_I));
        format->trigger();
        QCOMPARE(editor->text(),
                 QString("select 1;\nSELECT\n  a,\n  b\nFROM\n  t\nWHERE\n  x = 1"));
        QCOMPARE(editor->selectedText(), QString("SELECT\n  a,\n  b\nFROM\n  t\nWHERE\n  x = 1"));
        window.findChild<QAction*>("command_minify_sql")->trigger();
        QCOMPARE(editor->text(), QString("select 1;\nSELECT a, b FROM t WHERE x = 1"));
        editor->undo();
        QCOMPARE(editor->text(),
                 QString("select 1;\nSELECT\n  a,\n  b\nFROM\n  t\nWHERE\n  x = 1"));
    }
    void commentTogglesUseTheSelectedLines() {
        choscordb::MainWindow window;
        auto* editor = openEditor(window);
        QVERIFY(editor);
        editor->setText("select 1\nfrom t");
        editor->setSelection(0, 2, 1, 2);
        window.findChild<QAction*>("command_toggle_line_comment")->trigger();
        QCOMPARE(editor->text(), QString("-- select 1\n-- from t"));
        window.findChild<QAction*>("command_toggle_line_comment")->trigger();
        QCOMPARE(editor->text(), QString("select 1\nfrom t"));
        editor->setSelection(1, 5, 1, 6);
        window.findChild<QAction*>("command_toggle_block_comment")->trigger();
        QCOMPARE(editor->text(), QString("select 1\nfrom /* t */"));
    }
    void rowLimitCapsSelectAndShowsInTheFooter() {
        choscordb::MainWindow window;
        auto* editor = openEditor(window);
        QVERIFY(editor);
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        auto* run = window.findChild<QAction*>("runStatement");
        auto* limit = window.findChild<QComboBox*>("rowLimit");
        QVERIFY(limit);
        QVERIFY(!limit->isEnabled());
        workspace->connectSqlite(":memory:");
        QTRY_VERIFY(run->isEnabled());
        QVERIFY(limit->isEnabled());
        const auto index = limit->findData(100u);
        QVERIFY(index > 0);
        limit->setCurrentIndex(index);
        emit limit->activated(index);
        editor->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM n WHERE x < "
                        "500) SELECT x FROM n");
        run->trigger();
        auto* grid = window.findChild<QTableView*>("queryResults");
        QTRY_COMPARE(grid->model()->rowCount(), 100);
        QTRY_COMPARE(window.findChild<QLabel*>("executionRows")->text(),
                     QString("100 rows · limited to 100 rows"));
        limit->setCurrentIndex(0);
        emit limit->activated(0);
        run->trigger();
        QTRY_COMPARE(window.findChild<QLabel*>("executionRows")->text(), QString("500 rows"));
    }
    void theRowLimitIsKeptWithTheSavedConnection() {
        choscordb::MainWindow window;
        QVERIFY(openEditor(window));
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        auto* adapter = workspace->adapter();
        choscordb::SavedProfile profile;
        profile.id = "limited";
        profile.name = "Limited";
        profile.driver = "sqlite";
        profile.path = ":memory:";
        profile.rowLimit = 300;
        QSignalSpy saved(adapter, &choscordb::EngineAdapter::profileSaved);
        adapter->saveProfile(profile, 901);
        QTRY_COMPARE(saved.count(), 1);
        QVERIFY(workspace->connectSavedProfile(
            saved.takeFirst().at(1).value<choscordb::SavedProfile>()));
        auto* limit = window.findChild<QComboBox*>("rowLimit");
        QTRY_VERIFY(limit->isEnabled());
        QCOMPARE(limit->currentData().toUInt(), 300u);
        const auto index = limit->findData(1000u);
        limit->setCurrentIndex(index);
        emit limit->activated(index);
        QTRY_COMPARE(saved.count(), 1);
        const auto stored = saved.takeFirst().at(1).value<choscordb::SavedProfile>();
        QCOMPARE(stored.id, QString("limited"));
        QCOMPARE(stored.rowLimit, 1000u);
    }
    void formatErrorsReachTheMessagesAndKeepTheText() {
        choscordb::MainWindow window;
        auto* editor = openEditor(window);
        QVERIFY(editor);
        editor->setText("select 'open");
        window.findChild<QAction*>("command_format_sql")->trigger();
        QCOMPARE(editor->text(), QString("select 'open"));
        QVERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                    ->toPlainText()
                    .contains("unterminated"));
    }
};

QTEST_MAIN(SqlTextCommandsTest)
#include "sql_text_commands_test.moc"
