#include "app/appearance_controller.h"
#include "app/main_window.h"
#include "app/query_workspace.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTableView>
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
