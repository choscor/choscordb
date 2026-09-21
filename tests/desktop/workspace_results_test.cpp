#include "app/appearance_controller.h"
#include "app/application_data.h"
#include "app/main_window.h"
#include "app/query_workspace.h"
#include "app/updater.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/field/field.h"
#include "models/history_model.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/history_dock/history_dock.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/search_panel/search_panel.h"
#include "widgets/sql_editor/sql_editor.h"
#include "workspace_test.h"
#include "workspace_test_fixture.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

void WorkspaceTest::connectsExecutesPagesAndCopies() {
    WorkspaceFixture f;
    QTRY_COMPARE(f.connections.count(), 1);
    QVERIFY(f.connections.isEnabled());
    QVERIFY(f.mode.isEnabled());
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1001) "
              "SELECT x FROM n");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    QVERIFY(f.next.isEnabled());
    f.next.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("1001"));
    QCOMPARE(f.grid.model()->headerData(0, Qt::Vertical).toString(), QString("1001"));
    QVERIFY(!f.next.isEnabled());
    f.grid.show();
    f.grid.activateWindow();
    f.grid.setFocus();
    f.grid.selectionModel()->select(f.grid.model()->index(0, 0),
                                    QItemSelectionModel::ClearAndSelect);
    QTest::qWait(20);
    QTest::keyClick(&f.grid, Qt::Key_C, Qt::ControlModifier);
#ifdef Q_OS_MACOS
    QTest::keyClick(&f.grid, Qt::Key_C, Qt::MetaModifier);
#endif
    QTRY_COMPARE(QApplication::clipboard()->text(), QString("1001"));
    f.workspace.shutdown();
}

void WorkspaceTest::revisitsPreviousPageWithoutReexecutingQuery() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1001) "
              "SELECT x FROM n");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    f.next.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QTRY_VERIFY(f.previous.isEnabled());
    f.previous.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("1"));
    QCOMPARE(f.grid.model()->headerData(0, Qt::Vertical).toString(), QString("1"));
    QVERIFY(!f.previous.isEnabled());
    QVERIFY(f.next.isEnabled());
    f.next.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QCOMPARE(f.grid.model()->headerData(0, Qt::Vertical).toString(), QString("1001"));
    QVERIFY(!f.next.isEnabled());
}

void WorkspaceTest::byteLimitedPagesUseStoredRowOffsets() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<600) "
              "SELECT x, printf('%09000d',x) AS payload FROM n");
    QTRY_VERIFY(f.grid.model()->rowCount() > 0);
    const int firstPageRows = f.grid.model()->rowCount();
    QVERIFY(firstPageRows < 600);
    QVERIFY(f.next.isEnabled());
    QList<int> starts{1};
    int total = firstPageRows;
    while (f.next.isEnabled()) {
        QVERIFY(starts.size() < 10);
        starts.push_back(total + 1);
        f.next.click();
        QTRY_COMPARE(f.grid.model()->headerData(0, Qt::Vertical).toString(),
                     QString::number(total + 1));
        QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(),
                 QString::number(total + 1));
        QVERIFY(f.grid.model()->rowCount() > 0);
        total += f.grid.model()->rowCount();
    }
    QCOMPARE(total, 600);
    for (int page = starts.size() - 2; page >= 0; --page) {
        f.previous.click();
        QTRY_COMPARE(f.grid.model()->headerData(0, Qt::Vertical).toString(),
                     QString::number(starts[page]));
    }
    QCOMPARE(f.grid.model()->rowCount(), firstPageRows);
    QVERIFY(!f.previous.isEnabled());
}

void WorkspaceTest::transactionKeepsAlreadyStoredNextPageAccessible() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.mode.setCurrentIndex(1);
    f.execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1001) "
              "SELECT x FROM n");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    f.next.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    f.previous.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    f.commit.trigger();
    QTRY_VERIFY(f.messages.toPlainText().contains("committed"));
    QTRY_VERIFY(f.next.isEnabled());
    f.next.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("1001"));
}

void WorkspaceTest::disconnectClearsPageNavigation() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1001) "
              "SELECT x FROM n");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    f.next.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QVERIFY(f.previous.isEnabled());
    f.workspace.adapter()->disconnectConnection(f.connections.currentData().toULongLong());
    QTRY_COMPARE(f.connections.count(), 0);
    QVERIFY(!f.previous.isEnabled());
    QVERIFY(!f.next.isEnabled());
    QVERIFY(!f.cancel.isEnabled());
}

void WorkspaceTest::visiblePageRemainsBudgetedAfterCoreQueryRelease() {
    WorkspaceFixture f;
    std::optional<quint64> query;
    QObject::connect(f.workspace.adapter(), &choscordb::EngineAdapter::eventReady, &f.parent,
                     [&](const choscordb::BridgeEvent& event) {
                         if (event.kind == "schema")
                             query = event.id;
                     });
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("SELECT 42");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QTRY_VERIFY(query.has_value());
    f.workspace.adapter()->releaseQuery(*query);
    QTRY_COMPARE(f.workspace.adapter()->memoryUsage().source, quint64(0));
    QVERIFY(f.workspace.adapter()->memoryUsage().used > 0);
    QVERIFY(f.workspace.adapter()->memoryUsage().used < 1024 * 1024);
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("42"));
}

void WorkspaceTest::repeatedPagingReleasesReplacedViews() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1001) "
              "SELECT x FROM n");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    for (int i = 0; i < 12; ++i) {
        f.next.click();
        QTRY_COMPARE(f.grid.model()->rowCount(), 1);
        f.previous.click();
        QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
        QTRY_VERIFY(f.workspace.adapter()->memoryUsage().used < 2 * 1024 * 1024);
    }
    QVERIFY(f.workspace.adapter()->memoryUsage().peak <= 64 * 1024 * 1024);
}

void WorkspaceTest::nestedEventLoopKeepsLiveTransferReserved() {
    choscordb::EngineAdapter adapter;
    bool observed = false, retained = false, survived = false;
    QObject::connect(&adapter, &choscordb::EngineAdapter::eventReady, &adapter,
                     [&](const choscordb::BridgeEvent& event) {
                         if (event.kind == "connected")
                             adapter.execute(event.id, "SELECT 42");
                         if (event.kind != "schema")
                             return;
                         retained = adapter.retainTransfer(event.lease_id, 512);
                         const auto before = adapter.memoryUsage().used;
                         QEventLoop loop;
                         QTimer::singleShot(0, &loop, [&] {
                             adapter.releasePageLease(event.lease_id);
                             survived = adapter.memoryUsage().used == before;
                             loop.quit();
                         });
                         QTimer::singleShot(2000, &loop, &QEventLoop::quit);
                         loop.exec();
                         observed = true;
                     });
    QVERIFY(adapter.connectSqlite(":memory:").has_value());
    QTRY_VERIFY(observed);
    QVERIFY(retained);
    QVERIFY(survived);
    QTRY_COMPARE(adapter.memoryUsage().used, adapter.memoryUsage().source);
}

void WorkspaceTest::previousPageUsesSharedHotCache() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1001) "
              "SELECT x FROM n");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    f.next.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    const auto before = f.workspace.adapter()->cacheUsage();
    QVERIFY(before.residentBytes > 0);
    f.previous.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    QVERIFY(f.workspace.adapter()->cacheUsage().hits > before.hits);
    QVERIFY(f.workspace.adapter()->memoryUsage().used >=
            f.workspace.adapter()->cacheUsage().residentBytes);
}

void WorkspaceTest::deferredCellOpensBoundedDetailAndClosesOnNewQuery() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("SELECT zeroblob(200000)");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    f.grid.doubleClicked(f.grid.model()->index(0, 0));
    auto* dialog = f.parent.findChild<QDialog*>("valueDetail");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->isVisible());
    auto* preview = dialog->findChild<QTableView*>();
    QVERIFY(preview);
    QTRY_VERIFY(preview->model()->rowCount() > 0);
    QCOMPARE(preview->model()->data(preview->model()->index(0, 0)).toULongLong(), quint64(0));
    auto* next = dialog->findChild<QPushButton*>("valueNext");
    QVERIFY(next);
    QTRY_VERIFY(next->isEnabled());
    next->click();
    QTRY_VERIFY(preview->model()->rowCount() > 0);
    QTRY_VERIFY(preview->model()->data(preview->model()->index(0, 0)).toULongLong() > 0);
    QVERIFY(f.workspace.adapter()->memoryUsage().used < 2 * 1024 * 1024);
    QVERIFY(dialog->grab().save("native-value-detail.png"));
    f.execute("SELECT 7");
    QTRY_VERIFY(!dialog->isVisible());
    QTRY_COMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("7"));
}

void WorkspaceTest::deferredTextRemainsInspectableAfterCommitAndDisconnectClosesDetail() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.mode.setCurrentIndex(1);
    f.execute("SELECT replace(hex(zeroblob(40000)), '00', 'first' || char(10) || 'second é')");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    f.commit.trigger();
    QTRY_VERIFY(f.messages.toPlainText().contains("committed"));
    f.grid.activated(f.grid.model()->index(0, 0));
    auto* dialog = f.parent.findChild<QDialog*>("valueDetail");
    QVERIFY(dialog);
    auto* preview = dialog->findChild<QTableView*>();
    QVERIFY(preview);
    QTRY_VERIFY(preview->model()->rowCount() > 0);
    QVERIFY(preview->model()
                ->data(preview->model()->index(0, 1))
                .toString()
                .contains("first\\nsecond é"));
    auto* next = dialog->findChild<QPushButton*>("valueNext");
    auto* previous = dialog->findChild<QPushButton*>("valuePrevious");
    QVERIFY(next && previous);
    next->click();
    QTRY_VERIFY(previous->isEnabled());
    previous->click();
    QTRY_VERIFY(preview->model()->rowCount() > 0);
    QTRY_COMPARE(preview->model()->data(preview->model()->index(0, 0)).toULongLong(), quint64(0));
    f.workspace.adapter()->disconnectConnection(f.connections.currentData().toULongLong());
    QTRY_COMPARE(f.connections.count(), 0);
    QVERIFY(!dialog->isVisible());
    QCOMPARE(preview->model()->rowCount(), 0);
}

void WorkspaceTest::backwardTextWindowAlignsUtf8AndLongRowsScroll() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("SELECT replace(hex(zeroblob(100000)), '00', '€')");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    f.grid.activated(f.grid.model()->index(0, 0));
    auto* dialog = f.parent.findChild<QDialog*>("valueDetail");
    QVERIFY(dialog);
    auto* preview = dialog->findChild<QTableView*>();
    auto* next = dialog->findChild<QPushButton*>("valueNext");
    auto* previous = dialog->findChild<QPushButton*>("valuePrevious");
    QVERIFY(preview && next && previous);
    QTRY_VERIFY(next->isEnabled());
    QTRY_VERIFY(preview->horizontalScrollBar()->maximum() > 0);
    next->click();
    QTRY_VERIFY(next->isEnabled());
    next->click();
    QTRY_VERIFY(previous->isEnabled());
    previous->click();
    QTRY_VERIFY(next->isEnabled());
    const auto offset = preview->model()->data(preview->model()->index(0, 0)).toULongLong();
    QCOMPARE(offset % 3, quint64(0));
    QVERIFY(offset > 0 && offset < 131070);
    const auto content = preview->model()->data(preview->model()->index(0, 1)).toString();
    QVERIFY(content.startsWith("€€€"));
    QVERIFY(!content.contains("Invalid"));
}

void WorkspaceTest::exportsMysqlInsertDialectThroughDialog() {
    WorkspaceFixture f;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("SELECT 'path\\name' AS label");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QTRY_VERIFY(f.exportResult.isEnabled());
    f.exportResult.click();
    auto* dialog = f.parent.findChild<choscordb::ExportDialog*>("exportDialog");
    QVERIFY(dialog);
    dialog->findChild<QLineEdit*>("exportDestination")->setText(directory.filePath("mysql.sql"));
    auto* format = dialog->findChild<QComboBox*>("exportFormat");
    format->setCurrentIndex(format->findData("sql"));
    auto* dialect = dialog->findChild<QComboBox*>("exportDialect");
    QVERIFY(dialect->findData("mysql") >= 0);
    dialect->setCurrentIndex(dialect->findData("mysql"));
    dialog->findChild<QLineEdit*>("exportTable")->setText("order`items");
    dialog->findChild<QPushButton*>("exportStart")->click();
    QTRY_VERIFY(f.exportResult.isEnabled());
    QFile output(directory.filePath("mysql.sql"));
    QVERIFY(output.open(QIODevice::ReadOnly));
    const auto bytes = output.readAll();
    QVERIFY(bytes.startsWith("INSERT INTO `order``items` (`label`) VALUES ("));
    QVERIFY(!bytes.contains("\"order"));
}

void WorkspaceTest::exportsOriginalResultAfterBrowsing() {
    WorkspaceFixture f;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1001) "
              "SELECT x FROM n");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    f.next.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QTRY_VERIFY(f.exportResult.isEnabled());
    f.exportResult.click();
    auto* dialog = f.parent.findChild<choscordb::ExportDialog*>("exportDialog");
    QVERIFY(dialog);
    const auto destination = directory.filePath("rows.csv");
    dialog->startExportTo(destination, "csv");
    auto* status = dialog->findChild<QLabel*>("exportStatus");
    QVERIFY(status);
    QTRY_VERIFY(f.exportResult.isEnabled());
    QFile output(destination);
    QVERIFY2(output.open(QIODevice::ReadOnly), qPrintable(status->text()));
    const auto bytes = output.readAll();
    QCOMPARE(bytes.count('\n'), 1002);
    QVERIFY(bytes.startsWith("\"x\"\r\n\"1\"\r\n"));
    QVERIFY(bytes.endsWith("\"1001\"\r\n"));
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("1001"));
    QVERIFY(dialog->grab().save("native-export.png"));
}

void WorkspaceTest::exportCancellationPreservesExistingDestination() {
    WorkspaceFixture f;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto destination = directory.filePath("rows.csv");
    QFile output(destination);
    QVERIFY(output.open(QIODevice::WriteOnly));
    QCOMPARE(output.write("original"), qint64(8));
    output.close();
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1000000) "
              "SELECT x FROM n");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    f.exportResult.click();
    auto* dialog = f.parent.findChild<choscordb::ExportDialog*>("exportDialog");
    QVERIFY(dialog);
    QTimer confirmation;
    confirmation.setInterval(10);
    connect(&confirmation, &QTimer::timeout, dialog, [&confirmation] {
        for (auto* widget : {choscordb::design::DialogPresentation::activeDialog()})
            if (auto* box = qobject_cast<QMessageBox*>(widget)) {
                confirmation.stop();
                box->done(QMessageBox::Yes);
            }
    });
    confirmation.start();
    dialog->startExportTo(destination, "csv");
    auto* cancel = dialog->findChild<QPushButton*>("exportCancel");
    QVERIFY(cancel);
    QTRY_VERIFY(!confirmation.isActive());
    QTRY_VERIFY(cancel->isEnabled());
    cancel->click();
    QTRY_VERIFY(f.run.isEnabled());
    QVERIFY(output.open(QIODevice::ReadOnly));
    QCOMPARE(output.readAll(), QByteArray("original"));
    output.close();
    QTRY_COMPARE(
        QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(),
        1);
}

void WorkspaceTest::exportFailureLeavesResultUsable() {
    WorkspaceFixture f;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("SELECT 42 AS answer");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    f.exportResult.click();
    auto* dialog = f.parent.findChild<choscordb::ExportDialog*>("exportDialog");
    QVERIFY(dialog);
    dialog->startExportTo(directory.filePath("missing/rows.csv"), "csv");
    QTRY_VERIFY(!dialog->isRunning());
    auto* status = dialog->findChild<QLabel*>("exportStatus");
    QVERIFY(status);
    QVERIFY(!status->text().isEmpty());
    QVERIFY(status->text().contains("failed", Qt::CaseInsensitive));
    QCOMPARE(status->textFormat(), Qt::PlainText);
    QVERIFY(f.run.isEnabled());
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("42"));
    QVERIFY(QDir(directory.path())
                .entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)
                .isEmpty());
}
