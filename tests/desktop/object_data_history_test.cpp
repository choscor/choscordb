#include "app/main_window.h"
#include "app/object_data_workspace.h"
#include "app/query_workspace.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTableView>
#include <QtTest>
// The object data time selector for relations that keep row history.
class ObjectDataHistoryTest : public QObject {
    Q_OBJECT

  private slots:
    void historySelectorAppearsOnlyForHistoryRelationsAndSubmitsItsMode() {
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* run = window.findChild<QAction*>("runStatement");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        QTRY_VERIFY(run->isEnabled());
        editor->setText("CREATE TABLE people AS SELECT 1 AS id;");
        run->trigger();
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.resize(900, 400);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* summary = data.findChild<QLabel*>("objectDataSummary");
        auto* messages = data.findChild<QPlainTextEdit*>("objectDataMessages");
        auto* timeMode = data.findChild<QComboBox*>("objectDataTimeMode");
        auto* asOf = data.findChild<QLineEdit*>("objectDataAsOf");
        QVERIFY(grid && summary && messages && timeMode && asOf);
        const auto failure = [&] { return summary->toolTip() + messages->toPlainText(); };

        data.openObject(connection, R"(["main","people"])", "people");
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        QVERIFY(!timeMode->isVisibleTo(&data));

        data.openObject(connection, R"(["main","people"])", "people", "system_versioned");
        QVERIFY(timeMode->isVisibleTo(&data));
        QVERIFY(!asOf->isVisibleTo(&data));
        QCOMPARE(timeMode->currentIndex(), 0);
        QTRY_VERIFY(timeMode->isEnabled());
        QCOMPARE(grid->model()->rowCount(), 1);

        // A mode whose read cannot start must not label the current rows.
        sql->setExternalWork(true);
        timeMode->setCurrentIndex(2);
        QCOMPARE(timeMode->currentIndex(), 0);
        sql->setExternalWork(false);

        // SQLite cannot browse history, so Rust rejects the submitted mode.
        timeMode->setCurrentIndex(2);
        QTRY_VERIFY(failure().contains("History browsing is unavailable for this connection"));
        QTRY_VERIFY(timeMode->isEnabled());

        timeMode->setCurrentIndex(1);
        QVERIFY(asOf->isVisibleTo(&data));
        asOf->setText("yesterday");
        QTest::keyClick(asOf, Qt::Key_Return);
        QTRY_VERIFY(failure().contains(
            "Enter a time as YYYY-MM-DD HH:MM:SS with up to six fraction digits"));

        timeMode->setCurrentIndex(0);
        QVERIFY(!asOf->isVisibleTo(&data));
        QTRY_COMPARE(grid->model()->rowCount(), 1);

        QTRY_VERIFY(timeMode->isEnabled());
        timeMode->setCurrentIndex(2);
        QTRY_VERIFY(timeMode->isEnabled());
        data.openObject(connection, R"(["main","people"])", "people", "materialized_view");
        QVERIFY(!timeMode->isVisibleTo(&data));
        QCOMPARE(timeMode->currentIndex(), 0);
        QTRY_COMPARE(grid->model()->rowCount(), 1);
    }
};
QTEST_MAIN(ObjectDataHistoryTest)
#include "object_data_history_test.moc"
