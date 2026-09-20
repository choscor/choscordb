#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
namespace {
struct Fixture {
    QWidget window;
    QComboBox connections, mode;
    QAction run, cancel, commit, rollback, newConnection;
    QPushButton next, previous, exportResult;
    QLabel summary;
    QPlainTextEdit messages;
    QTableView grid;
    choscordb::SqlEditor editor;
    choscordb::QueryWorkspace workspace{{&connections,
                                         &mode,
                                         &run,
                                         &cancel,
                                         &commit,
                                         &rollback,
                                         &newConnection,
                                         &next,
                                         &summary,
                                         &messages,
                                         &grid,
                                         [this] { return &editor; },
                                         &window,
                                         &previous,
                                         &exportResult,
                                         {}},
                                        &window};
    Fixture() { mode.addItems({"Auto", "Manual"}); }
};
void answer(QObject* context, bool accept, bool* rollbackNotice = nullptr) {
    QTimer::singleShot(0, context, [accept, rollbackNotice] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        QVERIFY(box);
        QCOMPARE(box->textFormat(), Qt::PlainText);
        if (rollbackNotice)
            *rollbackNotice = box->text().contains("roll", Qt::CaseInsensitive);
        if (accept) {
            for (auto* button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
                    button->click();
                    return;
                }
        }
        box->button(QMessageBox::Cancel)->click();
    });
}
} // namespace
class DisconnectWorkspaceTest : public QObject {
    Q_OBJECT
  private slots:
    void suspendedResultRequiresCloseConfirmation() {
        Fixture f;
        QSignalSpy connected(&f.workspace, &choscordb::QueryWorkspace::connectionReady);
        qInfo("DISCONNECT_TRACE before connect");
        f.workspace.connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 1);
        qInfo("DISCONNECT_TRACE connected");
        f.editor.setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                         "x<1001) SELECT x FROM n");
        f.editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        f.run.trigger();
        qInfo("DISCONNECT_TRACE submitted");
        // Cold hosted macOS workers can exceed Qt Test's five-second default here.
        QTRY_COMPARE_WITH_TIMEOUT(f.grid.model()->rowCount(), 1000, 30000);
        qInfo("DISCONNECT_TRACE first page");
        QTRY_VERIFY(f.run.isEnabled());
        answer(&f.window, false);
        qInfo("DISCONNECT_TRACE before first confirmation");
        QVERIFY(!f.workspace.confirmShutdown());
        qInfo("DISCONNECT_TRACE after first confirmation");
        QCOMPARE(f.connections.count(), 1);
        QCOMPARE(f.grid.model()->rowCount(), 1000);
        f.next.click();
        QTRY_COMPARE(f.grid.model()->rowCount(), 1);
        qInfo("DISCONNECT_TRACE next page");
        f.previous.click();
        QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
        qInfo("DISCONNECT_TRACE previous page");
        // Archived navigation is not an unfinished database execution.
        answer(&f.window, false);
        QVERIFY(f.workspace.confirmShutdown());
        qInfo("DISCONNECT_TRACE final confirmation");
    }
    void transactionCancelPreservesWriteAndAcceptanceRollsItBack() {
        QTemporaryDir directory;
        const auto path = directory.filePath("transaction.sqlite");
        Fixture f;
        QSignalSpy connected(&f.workspace, &choscordb::QueryWorkspace::connectionReady);
        int finished = 0;
        connect(f.workspace.adapter(), &choscordb::EngineAdapter::eventReady, &f.window,
                [&](const choscordb::BridgeEvent& event) {
                    if (QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size())) ==
                        "query_finished")
                        ++finished;
                });
        f.workspace.connectSqlite(path);
        QTRY_COMPARE(connected.count(), 1);
        const auto id = connected.first().at(0).toULongLong();
        auto setup = f.workspace.adapter()->execute(id, "CREATE TABLE work(value INTEGER)");
        QVERIFY(setup);
        f.workspace.adapter()->fetchPage(*setup);
        QTRY_COMPARE(finished, 1);
        f.workspace.adapter()->releaseQuery(*setup);
        f.mode.setCurrentIndex(1);
        f.editor.setText("INSERT INTO work VALUES (7)");
        f.editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        f.run.trigger();
        QTRY_COMPARE(finished, 2);
        QTRY_VERIFY(f.commit.isEnabled());
        answer(&f.window, false);
        f.workspace.disconnectConnection(id);
        QCOMPARE(f.connections.count(), 1);
        f.editor.setText("SELECT count(*) FROM work");
        f.editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        f.run.trigger();
        QTRY_COMPARE(finished, 3);
        QCOMPARE(f.grid.model()->index(0, 0).data().toString(), QString("1"));
        QTimer::singleShot(0, &f.window, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            QVERIFY(box->defaultButton() == box->button(QMessageBox::Cancel));
            f.workspace.disconnectConnection(id); // Duplicate confirmation is ignored.
            for (auto* button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
                    QVERIFY(button->text().contains("Roll back"));
                    button->click();
                    return;
                }
            QFAIL("missing disconnect action");
        });
        const auto buffer = f.editor.text();
        f.workspace.disconnectConnection(id);
        f.workspace.disconnectConnection(id); // Accepted duplicate is ignored too.
        QTRY_COMPARE(f.connections.count(), 0);
        QCOMPARE(f.editor.text(), buffer);
        f.workspace.connectSqlite(path);
        QTRY_COMPARE(connected.count(), 2);
        QVERIFY(!f.connections.currentData().isValid());
        QVERIFY(!f.run.isEnabled());
        f.connections.setCurrentIndex(f.connections.findData(connected.last().at(0)));
        QTRY_VERIFY(f.run.isEnabled());
        f.editor.setText("SELECT count(*) FROM work");
        f.editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        f.run.trigger();
        QTRY_COMPARE(finished, 4);
        QCOMPARE(f.grid.model()->index(0, 0).data().toString(), QString("0"));
    }
    void activeWorkShowsCancellationAndDisconnects() {
        Fixture f;
        QSignalSpy connected(&f.workspace, &choscordb::QueryWorkspace::connectionReady);
        f.workspace.connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 1);
        const auto id = connected.first().at(0).toULongLong();
        f.workspace.connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 2);
        const auto other = connected.last().at(0).toULongLong();
        f.connections.setCurrentIndex(f.connections.findData(QVariant::fromValue<qulonglong>(id)));
        int schemas = 0;
        connect(f.workspace.adapter(), &choscordb::EngineAdapter::eventReady, &f.window,
                [&](const choscordb::BridgeEvent& event) {
                    if (QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size())) ==
                        "schema")
                        ++schemas;
                });
        f.editor.setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                         "x<100000000) SELECT sum(x) FROM n");
        f.editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        f.run.trigger();
        QTRY_COMPARE(schemas, 1);
        QTRY_VERIFY(f.cancel.isEnabled());
        QTimer::singleShot(0, &f.window, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            QVERIFY(box->text().contains("cancel", Qt::CaseInsensitive));
            for (auto* button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
                    button->click();
                    return;
                }
        });
        f.workspace.disconnectConnection(id);
        QVERIFY(f.connections.isEnabled());
        f.connections.setCurrentIndex(
            f.connections.findData(QVariant::fromValue<qulonglong>(other)));
        QVERIFY(f.run.isEnabled());
        f.editor.setText("SELECT 7");
        f.editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        f.run.trigger();
        QTRY_COMPARE(f.connections.count(), 1);
        QTRY_COMPARE(f.grid.model()->rowCount(), 1);
        QCOMPARE(f.grid.model()->index(0, 0).data().toString(), QString("7"));
    }
    void activeExportDisconnectCancelsAndRemovesTemporaryOutput() {
        Fixture f;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSignalSpy connected(&f.workspace, &choscordb::QueryWorkspace::connectionReady);
        f.workspace.connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 1);
        const auto id = connected.first().at(0).toULongLong();
        f.editor.setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                         "x<1000000) SELECT x FROM n");
        f.editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        f.run.trigger();
        // Generating the first page can exceed Qt Test's default on hosted macOS.
        QTRY_COMPARE_WITH_TIMEOUT(f.grid.model()->rowCount(), 1000, 30000);
        QTRY_VERIFY(f.exportResult.isEnabled());
        f.exportResult.click();
        auto* dialog = f.window.findChild<choscordb::ExportDialog*>("exportDialog");
        QVERIFY(dialog);
        int failedExports = 0;
        int completedExports = 0;
        int exportProgress = 0;
        connect(
            f.workspace.adapter(), &choscordb::EngineAdapter::eventReady, &f.window,
            [&](const choscordb::BridgeEvent& event) {
                const auto kind =
                    QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
                if (kind == "export_progress" && event.exported_rows > 0)
                    ++exportProgress;
                if (kind == "export_failed")
                    ++failedExports;
                if (kind == "export_finished")
                    ++completedExports;
            },
            Qt::DirectConnection);
        const auto destination = directory.filePath("rows.csv");
        dialog->startExportTo(destination, "csv");
        QTRY_VERIFY(dialog->isRunning());
        QTRY_VERIFY(exportProgress > 0); // Actual accepted export, not just destination preflight.
        QTimer::singleShot(0, &f.window, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            QVERIFY(box->text().contains("export", Qt::CaseInsensitive));
            QVERIFY(box->text().contains("cancel", Qt::CaseInsensitive));
            for (auto* button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
                    button->click();
                    return;
                }
            QFAIL("missing disconnect action");
        });
        f.workspace.disconnectConnection(id);
        QTRY_COMPARE(f.connections.count(), 0);
        QTRY_COMPARE(failedExports, 1);
        QCOMPARE(completedExports, 0);
        QTRY_VERIFY(!dialog->isRunning());
        QTRY_VERIFY(!dialog->isVisible());
        QVERIFY(!QFile::exists(destination));
        QTRY_VERIFY(QDir(directory.path())
                        .entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)
                        .isEmpty());
    }
    void targetRemovedDuringConfirmationIsRevalidatedBeforeSubmission() {
        Fixture f;
        QSignalSpy connected(&f.workspace, &choscordb::QueryWorkspace::connectionReady);
        QSignalSpy failures(f.workspace.adapter(), &choscordb::EngineAdapter::commandFailed);
        f.workspace.connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 1);
        const auto id = connected.first().at(0).toULongLong();
        QTimer::singleShot(0, &f.window, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            QVERIFY(f.workspace.adapter()->disconnectConnection(id));
            QTRY_COMPARE(f.connections.count(), 0);
            for (auto* button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
                    button->click();
                    return;
                }
            QFAIL("missing disconnect action");
        });
        f.workspace.disconnectConnection(id);
        QCOMPARE(f.connections.count(), 0);
        QCOMPARE(failures.count(), 0); // A second submission would report a stale handle.
    }
    void cancelThenDisconnectUnselectedSessionPreservesEditor() {
        Fixture f;
        QSignalSpy connected(&f.workspace, &choscordb::QueryWorkspace::connectionReady);
        f.workspace.connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 1);
        const auto first = connected.at(0).at(0).toULongLong();
        f.workspace.connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 2);
        f.connections.setCurrentIndex(f.connections.findData(connected.last().at(0)));
        const auto second = connected.at(1).at(0).toULongLong();
        f.editor.setText("-- untouched draft");
        bool warning = false;
        answer(&f.window, false, &warning);
        f.workspace.disconnectConnection(first);
        QVERIFY(warning);
        QCOMPARE(f.connections.count(), 2);
        QCOMPARE(f.connections.currentData().toULongLong(), second);
        answer(&f.window, true);
        f.workspace.disconnectConnection(first);
        QTRY_COMPARE(f.connections.count(), 1);
        QCOMPARE(f.connections.currentData().toULongLong(), second);
        QCOMPARE(f.editor.text(), QString("-- untouched draft"));
        f.workspace.disconnectConnection(first);
        QCOMPARE(f.connections.count(), 1);
    }
};
QTEST_MAIN(DisconnectWorkspaceTest)
#include "disconnect_workspace_test.moc"
