#include "app/diagnostics_file_lock.h"
#include "app/diagnostics_service.h"
#include "app/main_window.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "modern_ui_test.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

void ModernUiTest::helpOpensDiagnosticsExportSummary() {
    choscordb::MainWindow window;
    window.show();
    auto* help = window.findChild<QMenu*>("helpMenu");
    QVERIFY(help);
    auto* exportAction = help->findChild<QAction*>("exportDiagnostics");
    QVERIFY(exportAction);
    QCOMPARE(exportAction->text(), QString("Export Diagnostics…"));
    exportAction->trigger();
    auto* dialog = window.findChild<QDialog*>("diagnosticsExportDialog");
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QVERIFY(dialog->findChild<QLabel*>("diagnosticsSummary"));
    QVERIFY(dialog->findChild<QPushButton*>("diagnosticsSave"));
    QVERIFY(dialog->findChild<QPushButton*>("diagnosticsShowFolder"));
    QVERIFY(dialog->findChild<QPushButton*>("diagnosticsClear"));
    dialog->close();
}

void ModernUiTest::diagnosticsClearRequiresConfirmation() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    choscordb::DiagnosticsService diagnostics(storage.path(), "1.2.3");
    QVERIFY(diagnostics.start());
    diagnostics.record({.event = choscordb::DiagnosticEvent::Error});
    diagnostics.flush();
    QFile userData(storage.filePath("profiles.sqlite"));
    QVERIFY(userData.open(QIODevice::WriteOnly));
    QCOMPARE(userData.write("keep profiles"), qint64(13));
    userData.close();
    choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"), &diagnostics);
    window.show();
    window.findChild<QAction*>("exportDiagnostics")->trigger();
    auto* dialog = window.findChild<QDialog*>("diagnosticsExportDialog");
    QVERIFY(dialog);
    auto* clear = dialog->findChild<QPushButton*>("diagnosticsClear");
    QVERIFY(clear);
    QTRY_VERIFY(clear->isEnabled());
    clear->click();
    auto* confirmation = window.findChild<QMessageBox*>("diagnosticsClearConfirmation");
    QVERIFY(confirmation);
    QVERIFY(confirmation->isVisible());
    QVERIFY(confirmation->text().contains("diagnostic", Qt::CaseInsensitive));
    confirmation->button(QMessageBox::Yes)->click();
    QTRY_VERIFY(!diagnostics.preview().hasHistory);
    QVERIFY(QFile::exists(userData.fileName()));
    diagnostics.record({.event = choscordb::DiagnosticEvent::Error});
    QCOMPARE(diagnostics.preview().categoryCounts.value("error"), 1);
    dialog->close();
    diagnostics.stop();
}

void ModernUiTest::diagnosticsExportLetsUserChooseZipDestination() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    choscordb::DiagnosticsService diagnostics(storage.path(), "1.2.3");
    QVERIFY(diagnostics.start());
    choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"), &diagnostics);
    window.show();
    window.findChild<QAction*>("exportDiagnostics")->trigger();
    auto* dialog = window.findChild<QDialog*>("diagnosticsExportDialog");
    QVERIFY(dialog);
    auto* destination = dialog->findChild<QLineEdit*>("diagnosticsDestination");
    auto* browse = dialog->findChild<QPushButton*>("diagnosticsBrowse");
    auto* save = dialog->findChild<QPushButton*>("diagnosticsSave");
    QVERIFY(destination && browse && save);
    QVERIFY(!save->isEnabled());
    destination->setText(storage.filePath("report.zip"));
    QTRY_VERIFY(save->isEnabled());
    save->click();
    QTRY_VERIFY(QFile::exists(storage.filePath("report.zip")));
    auto* status = dialog->findChild<QLabel*>("diagnosticsStatus");
    QVERIFY(status);
    QTRY_VERIFY(status->text().contains("attach it manually"));
    dialog->close();
    diagnostics.stop();
}

void ModernUiTest::diagnosticsCountConnectionOutcomesFromWorkspace() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    choscordb::DiagnosticsService diagnostics(storage.path(), "1.2.3");
    QVERIFY(diagnostics.start());
    choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"), &diagnostics);
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QVERIFY(workspace);
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(connected.count(), 1);
    QCOMPARE(diagnostics.preview().categoryCounts.value("connection_succeeded"), 1);
    diagnostics.stop();
}

void ModernUiTest::diagnosticsRetainDriverTypeForFailedConnection() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    choscordb::DiagnosticsService diagnostics(storage.path(), "1.2.3");
    QVERIFY(diagnostics.start());
    choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"), &diagnostics);
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QVERIFY(workspace);
    workspace->connectSqlite(storage.filePath("missing/private-name.sqlite"));
    QTRY_COMPARE(diagnostics.preview().categoryCounts.value("connection_failed"), 1);
    const auto files = QDir(diagnostics.folderPath()).entryList({"*.jsonl"}, QDir::Files);
    QVERIFY(!files.isEmpty());
    QFile log(QDir(diagnostics.folderPath()).filePath(files.first()));
    QVERIFY(log.open(QIODevice::ReadOnly));
    const auto bytes = log.readAll();
    QVERIFY(bytes.contains("\"event\":\"connection_failed\""));
    QVERIFY(bytes.contains("\"driver\":\"sqlite\""));
    QVERIFY(!bytes.contains("private-name"));
    diagnostics.stop();
}

void ModernUiTest::diagnosticsExcludeSqlAndDriverErrorFromLocalReport() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    choscordb::DiagnosticsService diagnostics(storage.path(), "1.2.3");
    QVERIFY(diagnostics.start());
    choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"), &diagnostics);
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QTRY_VERIFY(recovery && recovery->isReady());
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QVERIFY(workspace);
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(connected.count(), 1);
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    QVERIFY(editor);
    auto* run = window.findChild<QAction*>("runStatement");
    QVERIFY(run);
    editor->setText("SELECT 'PRIVATE_ROW_SENTINEL_887' AS value");
    QTRY_VERIFY(run->isEnabled());
    run->trigger();
    QTRY_COMPARE(diagnostics.preview().categoryCounts.value("query_succeeded"), 1);
    QTRY_VERIFY(workspace->navigationAllowed());
    editor->setText("SELECT * FROM PRIVATE_ERROR_SENTINEL_229");
    QTRY_VERIFY(run->isEnabled());
    run->trigger();
    QTRY_COMPARE(diagnostics.preview().categoryCounts.value("query_failed"), 1);
    window.findChild<QAction*>("exportDiagnostics")->trigger();
    auto* dialog = window.findChild<QDialog*>("diagnosticsExportDialog");
    QVERIFY(dialog);
    const auto path = storage.filePath("diagnostic.zip");
    dialog->findChild<QLineEdit*>("diagnosticsDestination")->setText(path);
    QTRY_VERIFY(dialog->findChild<QPushButton*>("diagnosticsSave")->isEnabled());
    dialog->findChild<QPushButton*>("diagnosticsSave")->click();
    QTRY_VERIFY(QFile::exists(path));
    QFile archive(path);
    QVERIFY(archive.open(QIODevice::ReadOnly));
    QByteArray bytes = archive.readAll();
    const auto files = QDir(diagnostics.folderPath()).entryList({"*.jsonl"}, QDir::Files);
    QVERIFY(!files.isEmpty());
    bool failedQueryHasTimingBucket = false;
    for (const auto& file : files) {
        QFile log(QDir(diagnostics.folderPath()).filePath(file));
        QVERIFY(log.open(QIODevice::ReadOnly));
        const auto contents = log.readAll();
        bytes += contents;
        for (const auto& line : contents.split('\n')) {
            const auto event = QJsonDocument::fromJson(line).object();
            if (event.value("event") == "query_failed")
                failedQueryHasTimingBucket = event.value("duration_bucket").toString() != "unknown";
        }
    }
    QVERIFY(!bytes.contains("PRIVATE_ROW_SENTINEL_887"));
    QVERIFY(!bytes.contains("PRIVATE_ERROR_SENTINEL_229"));
    QVERIFY(bytes.contains("query_succeeded"));
    QVERIFY(bytes.contains("query_failed"));
    QVERIFY(failedQueryHasTimingBucket);
    dialog->close();
    diagnostics.stop();
}

void ModernUiTest::diagnosticsFolderOpened(const QUrl& url) {
    diagnosticsFolderUrl_ = url;
}

void ModernUiTest::diagnosticsShowFolderUsesLocalDiagnosticPath() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    choscordb::DiagnosticsService diagnostics(storage.path(), "1.2.3");
    QVERIFY(diagnostics.start());
    choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"), &diagnostics);
    window.show();
    window.findChild<QAction*>("exportDiagnostics")->trigger();
    auto* dialog = window.findChild<QDialog*>("diagnosticsExportDialog");
    QVERIFY(dialog);
    diagnosticsFolderUrl_.clear();
    QDesktopServices::setUrlHandler("file", this, "diagnosticsFolderOpened");
    struct HandlerReset {
        ~HandlerReset() { QDesktopServices::unsetUrlHandler("file"); }
    } reset;
    auto* folder = dialog->findChild<QPushButton*>("diagnosticsShowFolder");
    QVERIFY(folder);
    folder->click();
    QCOMPARE(diagnosticsFolderUrl_.toLocalFile(), diagnostics.folderPath());
    dialog->close();
    diagnostics.stop();
}

void ModernUiTest::diagnosticsClearCancelKeepsRecords() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    choscordb::DiagnosticsService diagnostics(storage.path(), "1.2.3");
    QVERIFY(diagnostics.start());
    diagnostics.record({.event = choscordb::DiagnosticEvent::Error});
    choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"), &diagnostics);
    window.show();
    window.findChild<QAction*>("exportDiagnostics")->trigger();
    auto* dialog = window.findChild<QDialog*>("diagnosticsExportDialog");
    QVERIFY(dialog);
    auto* clear = dialog->findChild<QPushButton*>("diagnosticsClear");
    QTRY_VERIFY(clear && clear->isEnabled());
    clear->click();
    auto* confirmation = window.findChild<QMessageBox*>("diagnosticsClearConfirmation");
    QVERIFY(confirmation);
    confirmation->button(QMessageBox::Cancel)->click();
    QCOMPARE(diagnostics.preview().categoryCounts.value("error"), 1);
    dialog->close();
    diagnostics.stop();
}

void ModernUiTest::diagnosticsSaveFailureLeavesNoZip() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    choscordb::DiagnosticsService diagnostics(storage.path(), "1.2.3");
    QVERIFY(diagnostics.start());
    choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"), &diagnostics);
    window.show();
    window.findChild<QAction*>("exportDiagnostics")->trigger();
    auto* dialog = window.findChild<QDialog*>("diagnosticsExportDialog");
    QVERIFY(dialog);
    const auto path = storage.filePath("absent/report.zip");
    dialog->findChild<QLineEdit*>("diagnosticsDestination")->setText(path);
    auto* save = dialog->findChild<QPushButton*>("diagnosticsSave");
    QTRY_VERIFY(save && save->isEnabled());
    save->click();
    auto* status = dialog->findChild<QLabel*>("diagnosticsStatus");
    QTRY_VERIFY(status && status->text().contains("failed"));
    QVERIFY(!QFile::exists(path));
    dialog->close();
    diagnostics.stop();
}

void ModernUiTest::diagnosticsCancelDuringExportLeavesNoZip() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    choscordb::DiagnosticsService diagnostics(storage.path(), "1.2.3");
    QVERIFY(diagnostics.start());
    const auto path = storage.filePath("cancelled-report.zip");
    {
        choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"), &diagnostics);
        window.show();
        window.findChild<QAction*>("exportDiagnostics")->trigger();
        auto* dialog = window.findChild<QDialog*>("diagnosticsExportDialog");
        QVERIFY(dialog);
        auto* destination = dialog->findChild<QLineEdit*>("diagnosticsDestination");
        auto* save = dialog->findChild<QPushButton*>("diagnosticsSave");
        auto* cancel = dialog->findChild<QPushButton*>("diagnosticsCancel");
        QVERIFY(destination && save && cancel);
        destination->setText(path);
        QTRY_VERIFY(save->isEnabled());
        choscordb::DiagnosticsFileLock lock(diagnostics.folderPath());
        QVERIFY(lock.lock(1000));
        save->click();
        cancel->click();
        lock.unlock();
    }
    QVERIFY(!QFile::exists(path));
    diagnostics.stop();
}

void ModernUiTest::diagnosticsPreviewAndClearKeepDialogResponsiveDuringStorageWait() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    choscordb::DiagnosticsService diagnostics(storage.path(), "1.2.3");
    QVERIFY(diagnostics.start());
    diagnostics.flush();
    choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"), &diagnostics);
    window.show();
    choscordb::DiagnosticsFileLock lock(diagnostics.folderPath());
    QVERIFY(lock.lock(1000));
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QVERIFY(workspace);
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    QElapsedTimer actionTime;
    actionTime.start();
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(connected.count(), 1);
    QVERIFY2(actionTime.elapsed() < 2500, "Connection action stalled behind diagnostics I/O");
    window.findChild<QAction*>("exportDiagnostics")->trigger();
    auto* dialog = window.findChild<QDialog*>("diagnosticsExportDialog");
    QVERIFY(dialog && dialog->isVisible());
    auto* summary = dialog->findChild<QLabel*>("diagnosticsSummary");
    QVERIFY(summary->text().contains("Reading"));
    lock.unlock();
    auto* clear = dialog->findChild<QPushButton*>("diagnosticsClear");
    QTRY_VERIFY(clear && clear->isEnabled());
    QVERIFY(lock.lock(1000));
    clear->click();
    auto* confirmation = window.findChild<QMessageBox*>("diagnosticsClearConfirmation");
    QVERIFY(confirmation);
    confirmation->button(QMessageBox::Yes)->click();
    auto* status = dialog->findChild<QLabel*>("diagnosticsStatus");
    QVERIFY(status->text().contains("Clearing"));
    QVERIFY(dialog->findChild<QPushButton*>("diagnosticsCancel")->isEnabled());
    lock.unlock();
    QTRY_VERIFY(status->text().contains("cleared"));
    dialog->close();
    diagnostics.stop();
}
