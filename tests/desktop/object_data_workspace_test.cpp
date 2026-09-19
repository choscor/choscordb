#include "app/main_window.h"
#include "app/object_data_workspace.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
class ObjectDataWorkspaceTest : public QObject {
    Q_OBJECT
  private slots:
    void shutdownConfirmsActiveObjectWorkWithoutDiscardingIt() {
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        auto* run = window.findChild<QAction*>("runStatement");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        QTRY_VERIFY(run->isEnabled());
        editor->setText("CREATE VIEW slow_object AS WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL "
                        "SELECT x+1 FROM n WHERE x<1000000000) SELECT sum(x) FROM n;");
        run->trigger();
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.show();
        data.openObject(connection, R"(["main","slow_object"])", "slow_object");
        QVERIFY(!sql->navigationAllowed());
        bool prompted = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            prompted = true;
            dialog->button(QMessageBox::Cancel)->click();
        });
        QVERIFY(!sql->confirmShutdown());
        QVERIFY(prompted);
        QVERIFY(!sql->navigationAllowed());
        auto* cancel = data.findChild<QPushButton*>("objectDataCancel");
        QTRY_VERIFY(cancel->isEnabled());
        cancel->click();
        QTRY_VERIFY(sql->navigationAllowed());
        QVERIFY(sql->confirmShutdown());
    }
    void copyExportAndValueDetailsUseObjectSource() {
        QTemporaryDir directory;
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        auto* run = window.findChild<QAction*>("runStatement");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* sqlTable = window.findChild<QTableView*>("queryResults");
        QTRY_VERIFY(run->isEnabled());
        editor->setText(
            "CREATE TABLE records AS SELECT 7 AS id, 'object' AS txt, zeroblob(70000) AS payload;");
        run->trigger();
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        editor->setText("SELECT 99 AS sql_source;");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        run->trigger();
        QTRY_COMPARE(sqlTable->model()->rowCount(), 1);
        QCOMPARE(sqlTable->model()->index(0, 0).data().toString(), QString("99"));
        QTRY_VERIFY(run->isEnabled());
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.resize(900, 600);
        data.show();
        data.openObject(connection, R"(["main","records"])", "records");
        auto* table = data.findChild<QTableView*>("objectDataResults");
        QTRY_COMPARE(table->model()->rowCount(), 1);
        QCOMPARE(table->verticalHeader()->sectionSize(0), 35);
        QCOMPARE(table->horizontalHeader()->height(), 43);
        QVERIFY(!table->showGrid());
        QVERIFY(!table->wordWrap());
        table->setCurrentIndex(table->model()->index(0, 0));
        table->setFocus();
        QTest::keySequence(table, QKeySequence::Copy);
        QCOMPARE(QApplication::clipboard()->text(), QString("7"));
        const auto payload = table->model()->index(0, 2);
        table->setCurrentIndex(payload);
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                          table->visualRect(payload).center());
        QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                           table->visualRect(payload).center());
        auto* detail = data.findChild<choscordb::ValueDetailDialog*>();
        QVERIFY(detail);
        QTRY_VERIFY(detail->isVisible());
        QVERIFY(!detail->isModal());
        QTRY_VERIFY(detail->findChild<QLabel*>("valueStatus")->text().contains("70000"));
        auto* preview = detail->findChild<QTableView*>("valuePreview");
        QTRY_VERIFY(preview->model()->rowCount() > 0);
        detail->close();
        auto* exportButton = data.findChild<QPushButton*>("objectDataExport");
        QTRY_VERIFY(exportButton->isEnabled());
        exportButton->click();
        auto* exportDialog = data.findChild<choscordb::ExportDialog*>();
        QVERIFY(exportDialog);
        QVERIFY(exportDialog->isModal());
        const auto destination = directory.filePath("objects.csv");
        exportDialog->findChild<QLineEdit*>("exportDestination")->setText(destination);
        exportDialog->findChild<QPushButton*>("exportStart")->click();
        QTRY_VERIFY(QFile::exists(destination));
        QTRY_VERIFY(!exportDialog->isRunning());
        QFile file(destination);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        QVERIFY(bytes.startsWith("\"id\",\"txt\",\"payload\"\r\n\"7\",\"object\","));
        exportDialog->close();
        QCOMPARE(sqlTable->model()->index(0, 0).data().toString(), QString("99"));
        data.invalidate();
        QCOMPARE(table->model()->rowCount(), 0);
        QVERIFY(!detail->isVisible());
    }
    void objectPagesKeepTheSqlResultAndItsCursor() {
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        auto* run = window.findChild<QAction*>("runStatement");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* sqlTable = window.findChild<QTableView*>("queryResults");
        QTRY_VERIFY(run->isEnabled());
        editor->setText("CREATE TABLE records AS WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT "
                        "x+1 FROM n WHERE x<1001) SELECT x FROM n;");
        run->trigger();
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        editor->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                        "x<1001) SELECT x+10000 FROM n;");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        run->trigger();
        QTRY_COMPARE(sqlTable->model()->rowCount(), 1000);
        QCOMPARE(sqlTable->model()->index(0, 0).data().toString(), QString("10001"));
        QTRY_VERIFY(run->isEnabled());
        const auto draft = editor->text();
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.show();
        data.openObject(connection, R"(["main","records"])", "records");
        auto* objectTable = data.findChild<QTableView*>("objectDataResults");
        QVERIFY(objectTable);
        QTRY_COMPARE(objectTable->model()->rowCount(), 1000);
        QCOMPARE(objectTable->model()->index(0, 0).data().toString(), QString("1"));
        QVERIFY(
            !(objectTable->model()->flags(objectTable->model()->index(0, 0)) & Qt::ItemIsEditable));
        QCOMPARE(sqlTable->model()->index(0, 0).data().toString(), QString("10001"));
        QCOMPARE(editor->text(), draft);
        auto* next = data.findChild<QPushButton*>("objectDataNext");
        QVERIFY(next);
        QTRY_VERIFY(next->isEnabled());
        next->click();
        QTRY_COMPARE(objectTable->model()->rowCount(), 1);
        QCOMPARE(objectTable->model()->index(0, 0).data().toString(), QString("1001"));
        auto* sqlNext = window.findChild<QPushButton*>("nextPage");
        QTRY_VERIFY(sqlNext->isEnabled());
        sqlNext->click();
        QTRY_COMPARE(sqlTable->model()->rowCount(), 1);
        QCOMPARE(sqlTable->model()->index(0, 0).data().toString(), QString("11001"));
        data.invalidate();
        QCOMPARE(objectTable->model()->rowCount(), 0);
        QCOMPARE(sqlTable->model()->index(0, 0).data().toString(), QString("11001"));
    }
};
QTEST_MAIN(ObjectDataWorkspaceTest)
#include "object_data_workspace_test.moc"
