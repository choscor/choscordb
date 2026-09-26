#include "workspace_test.h"
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
#include "design_system/toast_region/toast_region.h"
#include "models/history_model.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/history_dock/history_dock.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/search_panel/search_panel.h"
#include "widgets/sql_editor/sql_editor.h"
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
#include <algorithm>

void WorkspaceTest::unsavedConnectionsRetainTheirDriver() {
    WorkspaceFixture fixture;
    QTRY_VERIFY(fixture.run.isEnabled());
    const auto sqliteId = fixture.connections.currentData().toULongLong();
    QCOMPARE(fixture.workspace.driverForConnection(sqliteId), QString("sqlite"));
    fixture.workspace.showProfiles();
    auto* dialog = fixture.parent.findChild<choscordb::ProfileDialog*>();
    QVERIFY(dialog);
    choscordb::SavedProfile profile;
    profile.driver = "postgres";
    // Exercise the dialog's public submission boundary without a remote server.
    emit dialog->connectionSubmitted(profile, 123456, false);
    QVERIFY(fixture.workspace.profileIdForConnection(123456).isEmpty());
    QCOMPARE(fixture.workspace.driverForConnection(123456), QString("postgres"));
    QCOMPARE(fixture.workspace.driverForConnection(sqliteId), QString("sqlite"));
    QVERIFY(fixture.workspace.driverForConnection(987654).isEmpty());
}

void WorkspaceTest::developmentUpdaterHasNoEnabledActions_data() {
    QTest::addColumn<bool>("isolated");
    QTest::newRow("normal-development") << false;
    QTest::newRow("isolated-automation") << true;
}

void WorkspaceTest::developmentUpdaterHasNoEnabledActions() {
    QFETCH(bool, isolated);
    QTemporaryDir directory;
    choscordb::MainWindow window(nullptr, directory.filePath("updater.sqlite"));
    choscordb::installNativeUpdater(window, isolated);
    QVERIFY(!window.findChild<QAction*>("checkForUpdates"));
    QVERIFY(!window.findChild<QAction*>("automaticUpdateChecks"));
    QVERIFY(!window.findChild<QAction*>("installDownloadedUpdate"));
}

void WorkspaceTest::productionDataUsesOneIdentityDirectory() {
    const auto originalOrganization = QCoreApplication::organizationName();
    QCoreApplication::setOrganizationName("Unrelated Qt organization");
    const auto actual = choscordb::applicationDataDirectory();
    QCoreApplication::setOrganizationName(originalOrganization);
    QCOMPARE(actual, QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                         .filePath("com.choscor.ChoscorDB"));
}

void WorkspaceTest::updateAppearanceFailurePostponesInstallation() {
    QTemporaryDir directory;
    const auto path = directory.filePath("failed-update.sqlite");
    choscordb::MainWindow window(nullptr, path);
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QTRY_VERIFY(recovery->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(editor);
    choscordb::EngineAdapter fixture(nullptr, directory.filePath("fixture.sqlite"));
    bool connected = false, executed = false;
    connect(&fixture, &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& event) {
                connected = connected || event.kind == "connected";
                executed = executed || event.kind == "query_finished";
            });
    const auto connection = fixture.connectSqlite(path);
    QVERIFY(connection);
    QTRY_VERIFY(connected);
    const auto query = fixture.execute(
        *connection, "CREATE TRIGGER reject_appearance BEFORE INSERT ON appearance_layout "
                     "BEGIN SELECT RAISE(ABORT, 'fixture persistence failure'); END");
    QVERIFY(query);
    fixture.fetchPage(*query);
    QTRY_VERIFY(executed);
    auto* appearance = window.findChild<choscordb::AppearanceController*>();
    QTRY_VERIFY(appearance->isReady());
    QSignalSpy failures(appearance, &choscordb::AppearanceController::warningChanged);
    window.resize(window.width() + 100, window.height() + 50);
    int installs = 0;
    window.requestUpdateRestart([&] { ++installs; });
    QTRY_VERIFY(!failures.isEmpty());
    QTest::qWait(100);
    QCOMPARE(installs, 0);
    QVERIFY(window.isVisible());
    QVERIFY(window.isEnabled());
    QVERIFY(editor->isEnabled());
}

void WorkspaceTest::updatePersistenceFailureKeepsWorkspaceUsable() {
    QTemporaryDir directory;
    const auto path = directory.filePath("failed-update.sqlite");
    choscordb::MainWindow window(nullptr, path);
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QTRY_VERIFY(recovery->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(editor);
    choscordb::EngineAdapter fixture(nullptr, directory.filePath("fixture.sqlite"));
    bool connected = false, executed = false;
    connect(&fixture, &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& event) {
                connected = connected || event.kind == "connected";
                executed = executed || event.kind == "query_finished";
            });
    const auto connection = fixture.connectSqlite(path);
    QVERIFY(connection);
    QTRY_VERIFY(connected);
    const auto query = fixture.execute(
        *connection, "CREATE TRIGGER reject_recovery BEFORE INSERT ON editor_documents "
                     "BEGIN SELECT RAISE(ABORT, 'fixture persistence failure'); END");
    QVERIFY(query);
    fixture.fetchPage(*query);
    QTRY_VERIFY(executed);
    editor->setText("SELECT 'must not discard'");
    QSignalSpy failures(recovery, &choscordb::WorkspaceRecoveryController::errorOccurred);
    int installs = 0;
    window.requestUpdateRestart([&] { ++installs; });
    QTRY_VERIFY(!failures.isEmpty());
    QTRY_VERIFY(editor->isEnabled());
    QCOMPARE(installs, 0);
    QVERIFY(window.isVisible());
    QVERIFY(!window.findChild<QAction*>("closeWithoutRecovery")->isEnabled());
    QCOMPARE(editor->text(), QString("SELECT 'must not discard'"));
}

void WorkspaceTest::updateRestartWaitsForRecoveryAndDatabaseShutdown() {
    QTemporaryDir directory;
    const auto path = directory.filePath("update.sqlite");
    choscordb::MainWindow window(nullptr, path);
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QTRY_VERIFY(recovery->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(editor);
    editor->setText("SELECT 'preserved across update'");
    auto* adapter = window.findChild<choscordb::QueryWorkspace*>()->adapter();
    QSignalSpy saved(adapter, &choscordb::EngineAdapter::workspaceSaved);
    QSignalSpy stopped(adapter, &choscordb::EngineAdapter::shutdownReady);
    int installs = 0;
    window.requestUpdateRestart([&] {
        QVERIFY(!saved.isEmpty());
        QCOMPARE(stopped.count(), 1);
        ++installs;
    });
    QCOMPARE(installs, 0);
    QTRY_COMPARE(installs, 1);
    choscordb::EngineAdapter reopened(nullptr, path);
    QSignalSpy restored(&reopened, &choscordb::EngineAdapter::workspaceTabsRestored);
    QVERIFY(reopened.restoreWorkspaceTabs(981));
    QTRY_COMPARE(restored.count(), 1);
    const auto documents = qvariant_cast<QList<choscordb::SavedWorkspaceTab>>(restored[0][1]);
    QCOMPARE(documents.size(), 1);
    QCOMPARE(documents[0].document.sql, QString("SELECT 'preserved across update'"));
}

void WorkspaceTest::formErrorsAppearBelowTheRelevantFields() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog profile(&adapter);
    profile.show();
    QTRY_VERIFY(profile.findChild<QPushButton*>("profileTest")->isEnabled());
    choscordb::SavedProfile blank;
    profile.testDraft(blank);
    auto* name = profile.findChild<QLineEdit*>("profileName");
    auto* nameValidation = dynamic_cast<choscordb::design::FieldValidation*>(name->parentWidget());
    QVERIFY(nameValidation);
    QVERIFY(nameValidation->error().contains("profile name"));
    name->setText("Valid name");
    QCOMPARE(nameValidation->error(), QString());

    choscordb::ExportDialog exportDialog(&adapter);
    exportDialog.setQuery(1);
    exportDialog.startExportTo({}, "csv");
    auto* destination = exportDialog.findChild<QLineEdit*>("exportDestination");
    auto* destinationValidation =
        dynamic_cast<choscordb::design::FieldValidation*>(destination->parentWidget());
    QVERIFY(destinationValidation);
    QVERIFY(destinationValidation->error().contains("destination"));
    destination->setText("/tmp/example.csv");
    QCOMPARE(destinationValidation->error(), QString());
}

void WorkspaceTest::newConnectionAfterSavingCreatesAnotherProfile() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>();
    QVERIFY(dialog);
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    auto* name = dialog->findChild<QLineEdit*>("profileName");
    auto* path = dialog->findChild<QLineEdit*>("profilePath");
    auto* profiles = dialog->findChild<QListWidget*>("profileList");
    QTRY_VERIFY(save->isEnabled());
    name->setText("First saved connection");
    path->setText(":memory:");
    save->click();
    QTRY_VERIFY(save->isEnabled());
    QTRY_COMPARE(profiles->count(), 1);
    dialog->reject();
    f.newConnection.trigger();
    QTRY_VERIFY(save->isEnabled());
    QVERIFY2(name->text().isEmpty(), "New connection must start a new profile identity");
    name->setText("Second saved connection");
    path->setText(":memory:");
    save->click();
    QTRY_VERIFY(save->isEnabled());
    QTRY_COMPARE(profiles->count(), 2);
    QStringList names;
    for (int row = 0; row < profiles->count(); ++row)
        names << profiles->item(row)->text();
    QVERIFY(names.contains("First saved connection"));
    QVERIFY(names.contains("Second saved connection"));
}

void WorkspaceTest::cancellationRacingSuccessfulCompletionReleasesNavigation() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    bool observed = false;
    bool runAvailable = false;
    bool navigationAvailable = false;
    auto* adapter = f.workspace.adapter();
    connect(adapter, &choscordb::EngineAdapter::eventReady, &f.parent,
            [&](const choscordb::BridgeEvent& event) {
                if (event.kind != "schema" || observed)
                    return;
                observed = true;
                // The real schema starts a page fetch. Simulate the transport
                // delivering its final page after Cancel, followed by successful
                // completion that already won the race in the engine.
                f.cancel.trigger();
                choscordb::BridgeEvent page;
                page.kind = "stored_page";
                page.id = event.id;
                adapter->eventReady(page);
                choscordb::BridgeEvent finished;
                finished.kind = "query_finished";
                finished.id = event.id;
                adapter->eventReady(finished);
                runAvailable = f.run.isEnabled();
                navigationAvailable = f.workspace.navigationAllowed();
            });
    f.execute("SELECT 42");
    QTRY_VERIFY(observed);
    QVERIFY2(runAvailable, "A completed query must release a cancelled outstanding page fetch");
    QVERIFY(navigationAvailable);
}

void WorkspaceTest::immediateCancellationDoesNotReenableCancelBeforeAcknowledgement() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.editor.setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                     "x<1000000000) SELECT sum(x) FROM n;");
    f.run.trigger();
    QVERIFY(f.cancel.isEnabled());
    bool reenabled = false;
    connect(&f.cancel, &QAction::changed, &f.parent, [&] {
        if (f.cancel.isEnabled())
            reenabled = true;
    });
    f.cancel.trigger();
    QVERIFY(!f.cancel.isEnabled());
    QTRY_VERIFY(f.run.isEnabled());
    QVERIFY2(!reenabled, "Queued/running events must not undo a user's pending cancellation");
    QCOMPARE(f.summary.property("state").toString(), QString("cancelled"));
}

void WorkspaceTest::connectionPanelRetainsFailedSaveConnectDraftAndRetries() {
    QTemporaryDir directory;
    WorkspaceFixture f;
    f.parent.resize(960, 640);
    f.parent.show();
    QTRY_VERIFY(f.run.isEnabled());
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>();
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->findChild<QPushButton*>("profileSaveConnect")->isEnabled());
    QCOMPARE(choscordb::design::DialogPresentation::activeDialog(), dialog);
    QVERIFY(!dialog->isWindow());
    QCOMPARE(dialog->window(), &f.parent);
    QVERIFY(f.parent.rect().contains(QRect(dialog->mapTo(&f.parent, QPoint()), dialog->size())));
    auto* saveConnect = dialog->findChild<QPushButton*>("profileSaveConnect");
    QTRY_VERIFY(saveConnect->isEnabled());
    auto* name = dialog->findChild<QLineEdit*>("profileName");
    auto* path = dialog->findChild<QLineEdit*>("profilePath");
    name->setText("Retry SQLite");
    const auto badPath = directory.filePath("missing/database.sqlite");
    path->setText(badPath);
    QSignalSpy opened(dialog, &choscordb::ProfileDialog::openQueryRequested);
    saveConnect->click();
    QTRY_VERIFY(saveConnect->isEnabled());
    QVERIFY(dialog->isVisible());
    QCOMPARE(opened.count(), 0);
    QCOMPARE(name->text(), QString("Retry SQLite"));
    QCOMPARE(path->text(), badPath);
    QVERIFY(!dialog->findChild<QLabel*>("profileStatus")->text().isEmpty());
    path->setText(directory.filePath("retry.sqlite"));
    saveConnect->click();
    QTRY_COMPARE(opened.count(), 1);
    QTRY_VERIFY(!dialog->isVisible());
    auto* completion = f.parent.findChild<choscordb::ToastRegion*>(
        "toastRegion", Qt::FindDirectChildrenOnly);
    QVERIFY(completion && completion->isVisible());
    QVERIFY(completion->text().contains("Connected."));
}

void WorkspaceTest::saveAndConnectPersistsProfileBeforeOpeningSession() {
    QTemporaryDir directory;
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>();
    QVERIFY(dialog);
    auto* saveConnect = dialog->findChild<QPushButton*>("profileSaveConnect");
    QVERIFY2(saveConnect, "Connection dialog must offer Save & connect");
    QTRY_VERIFY(saveConnect->isEnabled());
    dialog->findChild<QLineEdit*>("profileName")->setText("Saved and connected");
    dialog->findChild<QLineEdit*>("profilePath")->setText(directory.filePath("saved.sqlite"));
    QSignalSpy submitted(dialog, &choscordb::ProfileDialog::connectionSubmitted);
    QSignalSpy connected(&f.workspace, &choscordb::QueryWorkspace::connectionReady);
    saveConnect->click();
    QTRY_COMPARE(connected.count(), 1);
    QCOMPARE(submitted.count(), 1);
    QVERIFY(submitted.first().at(2).toBool());
    QSignalSpy profiles(f.workspace.adapter(), &choscordb::EngineAdapter::profilesReady);
    f.workspace.adapter()->listProfiles(987654);
    QTRY_COMPARE(profiles.count(), 1);
    const auto saved = qvariant_cast<QList<choscordb::SavedProfile>>(profiles.first().at(1));
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.first().name, QString("Saved and connected"));
    QCOMPARE(saved.first().path, directory.filePath("saved.sqlite"));
    QTRY_VERIFY(!dialog->isVisible());
}

void WorkspaceTest::recoveryAdapterRejectsOversizeBeforeDispatch() {
    choscordb::EngineAdapter adapter;
    QSignalSpy failures(&adapter, &choscordb::EngineAdapter::recoveryFailed);
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::workspaceSaved);
    QList<choscordb::SavedEditorDocument> documents;
    for (int i = 0; i < 129; ++i)
        documents.push_back({QString::number(i), "Query", "SELECT 1", {}, {}, 0, 0, false});
    QVERIFY(!adapter.saveWorkspace(documents, 91));
    QCOMPARE(failures.count(), 1);
    QCOMPARE(failures.at(0).at(0).toULongLong(), quint64(91));
    documents = {{"valid", "Query", "SELECT 'é';", {}, {}, 10, 8, true}};
    QVERIFY(adapter.saveWorkspace(documents, 92));
    QTRY_COMPARE(saved.count(), 1);
    QCOMPARE(saved.at(0).at(0).toULongLong(), quint64(92));
    QSignalSpy restored(&adapter, &choscordb::EngineAdapter::workspaceRestored);
    QVERIFY(adapter.restoreWorkspace(93));
    QTRY_COMPARE(restored.count(), 1);
    const auto result = qvariant_cast<QList<choscordb::SavedEditorDocument>>(restored.at(0).at(1));
    QCOMPARE(result.size(), 1);
    QCOMPARE(result[0].sql, QString("SELECT 'é';"));
    QCOMPARE(result[0].cursorOffset, quint64(10));
    QCOMPARE(result[0].selectionAnchor, quint64(8));
    QVERIFY(result[0].modified);
}

void WorkspaceTest::sqlStartedTransactionRequiresCloseConfirmation() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    const QString begin = "BEGIN TRANSACTION";
    if (choscordb::EngineAdapter::executionRange(begin, 0, 0, 0).confirmation)
        QTimer::singleShot(0, &f.parent, [] {
            for (auto* widget : {choscordb::design::DialogPresentation::activeDialog()})
                if (auto* box = qobject_cast<QMessageBox*>(widget))
                    box->button(QMessageBox::Yes)->click();
        });
    f.editor.setText(begin);
    f.run.trigger();
    QTRY_VERIFY(f.messages.toPlainText().contains("Completed"));
    bool asked = false;
    QTimer::singleShot(0, &f.parent, [&] {
        for (auto* widget : {choscordb::design::DialogPresentation::activeDialog()})
            if (auto* box = qobject_cast<QMessageBox*>(widget)) {
                asked = true;
                box->button(QMessageBox::Cancel)->click();
            }
    });
    QVERIFY(!f.workspace.confirmShutdown());
    QVERIFY(asked);
    f.messages.clear();
    f.editor.setText("COMMIT");
    if (choscordb::EngineAdapter::executionRange("COMMIT", 0, 0, 0).confirmation)
        QTimer::singleShot(0, &f.parent, [] {
            for (auto* widget : {choscordb::design::DialogPresentation::activeDialog()})
                if (auto* box = qobject_cast<QMessageBox*>(widget))
                    box->button(QMessageBox::Yes)->click();
        });
    f.run.trigger();
    QTRY_VERIFY(f.messages.toPlainText().contains("Completed"));
    QVERIFY(f.workspace.confirmShutdown());
}

void WorkspaceTest::transactionCloseRequiresExplicitChoice() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.mode.setCurrentIndex(1);
    f.execute("SELECT 1");
    QTRY_VERIFY(f.run.isEnabled());
    bool cancelled = false;
    QTimer cancelTimer;
    cancelTimer.setInterval(10);
    connect(&cancelTimer, &QTimer::timeout, &f.parent, [&] {
        for (auto* widget : {choscordb::design::DialogPresentation::activeDialog()}) {
            auto* box = qobject_cast<QMessageBox*>(widget);
            if (!box || !box->isVisible())
                continue;
            if (auto* button = box->button(QMessageBox::Cancel)) {
                cancelled = true;
                cancelTimer.stop();
                button->click();
                return;
            }
        }
    });
    cancelTimer.start();
    QVERIFY(!f.workspace.confirmShutdown());
    QVERIFY(cancelled);
    QVERIFY(f.run.isEnabled());
    bool approved = false;
    QTimer approveTimer;
    approveTimer.setInterval(10);
    connect(&approveTimer, &QTimer::timeout, &f.parent, [&] {
        for (auto* widget : {choscordb::design::DialogPresentation::activeDialog()}) {
            auto* box = qobject_cast<QMessageBox*>(widget);
            if (!box || !box->isVisible())
                continue;
            for (auto* button : box->buttons()) {
                if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
                    approved = true;
                    approveTimer.stop();
                    button->click();
                    return;
                }
            }
        }
    });
    approveTimer.start();
    QVERIFY(f.workspace.confirmShutdown());
    QVERIFY(approved);
    QSignalSpy transactionState(&f.workspace, &choscordb::QueryWorkspace::transactionStateChanged);
    f.rollback.trigger();
    QTRY_VERIFY(std::any_of(transactionState.begin(), transactionState.end(),
                            [](const auto& args) { return !args.at(1).toBool(); }));
    QTRY_VERIFY(f.messages.toPlainText().contains("rolled back"));
    QTRY_VERIFY(f.run.isEnabled());
    bool unexpectedPrompt = false;
    QTimer finalTimer;
    finalTimer.setInterval(10);
    connect(&finalTimer, &QTimer::timeout, &f.parent, [&] {
        for (auto* widget : {choscordb::design::DialogPresentation::activeDialog()}) {
            auto* box = qobject_cast<QMessageBox*>(widget);
            if (!box || !box->isVisible())
                continue;
            if (auto* button = box->button(QMessageBox::Cancel)) {
                unexpectedPrompt = true;
                finalTimer.stop();
                button->click();
                return;
            }
        }
    });
    finalTimer.start();
    QVERIFY(f.workspace.confirmShutdown());
    finalTimer.stop();
    QVERIFY(!unexpectedPrompt);
}

void WorkspaceTest::mainWindowHistoryRecordsOpensDisablesAndFlushes() {
    QTemporaryDir directory;
    const auto path = directory.filePath("metadata.sqlite");
    choscordb::MainWindow window(nullptr, path);
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QTRY_VERIFY(recovery->isReady());
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* adapter = workspace->adapter();
    auto* run = window.findChild<QAction*>("runStatement");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* history = window.findChild<choscordb::HistoryDock*>();
    QVERIFY(history);
    int finished = 0;
    connect(
        adapter, &choscordb::EngineAdapter::eventReady, &window,
        [&](const choscordb::BridgeEvent& event) {
            if (QString::fromUtf8(event.kind.data(), static_cast<qsizetype>(event.kind.size())) ==
                "query_finished")
                ++finished;
        });
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    QTRY_VERIFY(run->isEnabled());
    auto executeSql = [&](const QString& sql) {
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        editor->setText(sql);
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        const int before = finished;
        run->trigger();
        QTRY_VERIFY(finished > before);
    };
    executeSql("SELECT 1 UNION ALL SELECT 2");
    history->show();
    history->refresh();
    auto* table = history->findChild<QTableView*>("historyTable");
    auto* model = qobject_cast<choscordb::HistoryModel*>(table->model());
    QTRY_COMPARE(model->rowCount(), 1);
    QCOMPARE(model->entry(0)->sql, QString("SELECT 1 UNION ALL SELECT 2"));
    QCOMPARE(model->entry(0)->status, QString("completed"));
    QVERIFY(model->entry(0)->hasRowCount);
    QCOMPARE(model->entry(0)->rowCount, quint64(2));
    table->selectRow(0);
    QVERIFY(window.grab().save("native-history.png"));
    auto* target = window.findChild<QComboBox*>("connectionSelector");
    const auto connection = target->currentData();
    const int queriesBeforeOpen = finished, tabsBeforeOpen = tabs->count();
    history->findChild<QPushButton*>("openHistoryQuery")->click();
    QCOMPARE(tabs->count(), tabsBeforeOpen + 1);
    QCOMPARE(qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget())->text(),
             QString("SELECT 1 UNION ALL SELECT 2"));
    QTest::qWait(30);
    QCOMPARE(finished, queriesBeforeOpen);
    QVERIFY(!target->currentData().isValid());
    QVERIFY(target->placeholderText().contains("Disconnected"));
    QVERIFY(!run->isEnabled());
    target->setCurrentIndex(target->findData(connection));
    QTRY_VERIFY(run->isEnabled());
    auto* record = window.findChild<QCheckBox*>("recordHistory");
    QTRY_VERIFY(record->isEnabled());
    record->click();
    QTRY_VERIFY(record->isEnabled());
    QVERIFY(!record->isChecked());
    executeSql("SELECT 3");
    history->refresh();
    QTRY_VERIFY(window.findChild<QPushButton*>("refreshHistory")->isEnabled());
    QCOMPARE(model->rowCount(), 1);
    QTimer::singleShot(0, &window, [] {
        for (auto* widget : {choscordb::design::DialogPresentation::activeDialog()})
            if (auto* box = qobject_cast<QMessageBox*>(widget))
                box->button(QMessageBox::Yes)->click();
    });
    history->findChild<QPushButton*>("clearHistory")->click();
    QTRY_COMPARE(model->rowCount(), 0);
    QTRY_VERIFY(record->isEnabled());
    record->click();
    QTRY_VERIFY(record->isEnabled());
    QVERIFY(record->isChecked());
    executeSql("SELECT 4");
    window.close();
    QTRY_VERIFY(!window.isVisible());
    choscordb::EngineAdapter reopened(nullptr, path);
    QSignalSpy entries(&reopened, &choscordb::EngineAdapter::historyListed);
    QVERIFY(reopened.listHistory(100, 0, 717));
    QTRY_COMPARE(entries.count(), 1);
    const auto persisted = qvariant_cast<QList<choscordb::SavedHistoryEntry>>(entries.at(0).at(1));
    QCOMPARE(persisted.size(), 1);
    QCOMPARE(persisted[0].sql, QString("SELECT 4"));
}

void WorkspaceTest::cancelledUpdateDoesNotInstallOnLaterNormalClose() {
    QTemporaryDir directory;
    const auto path = directory.filePath("metadata.sqlite");
    choscordb::MainWindow window(nullptr, path);
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QTRY_VERIFY(recovery->isReady());
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* run = window.findChild<QAction*>("runStatement");
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    QTRY_VERIFY(run->isEnabled());
    bool running = false;
    connect(workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& e) {
                if (QString::fromUtf8(e.kind.data(), static_cast<qsizetype>(e.kind.size())) ==
                        "query_state" &&
                    QString::fromUtf8(e.state.data(), static_cast<qsizetype>(e.state.size())) ==
                        "running")
                    running = true;
            });
    const QString sql = "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                        "x<100000000) SELECT sum(x) FROM n";
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    editor->setText(sql);
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
    run->trigger();
    QTRY_VERIFY(running);
    bool cancelled = false;
    QTimer chooser;
    chooser.setInterval(5);
    connect(&chooser, &QTimer::timeout, &window, [&] {
        for (auto* widget : {choscordb::design::DialogPresentation::activeDialog()})
            if (auto* box = qobject_cast<QMessageBox*>(widget)) {
                if (!cancelled) {
                    cancelled = true;
                    box->button(QMessageBox::Cancel)->click();
                    chooser.stop();
                    return;
                }
                for (auto* button : box->buttons())
                    if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
                        chooser.stop();
                        button->click();
                        return;
                    }
            }
    });
    int installs = 0;
    chooser.start();
    window.requestUpdateRestart([&] { ++installs; });
    QTRY_VERIFY(cancelled);
    QCOMPARE(installs, 0);
    QVERIFY(window.isVisible());
    QVERIFY(window.isEnabled());
    QVERIFY(editor->isEnabled());
    // A later ordinary close must not reuse the cancelled installation approval.
    chooser.start();
    window.close();
    QTRY_VERIFY(!window.isVisible());
    QCOMPARE(installs, 0);
}

void WorkspaceTest::closingActiveQueryFlushesDisconnectedHistory() {
    QTemporaryDir directory;
    const auto path = directory.filePath("metadata.sqlite");
    choscordb::MainWindow window(nullptr, path);
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QTRY_VERIFY(recovery->isReady());
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* run = window.findChild<QAction*>("runStatement");
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    QTRY_VERIFY(run->isEnabled());
    bool running = false;
    connect(workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& e) {
                if (QString::fromUtf8(e.kind.data(), static_cast<qsizetype>(e.kind.size())) ==
                        "query_state" &&
                    QString::fromUtf8(e.state.data(), static_cast<qsizetype>(e.state.size())) ==
                        "running")
                    running = true;
            });
    const QString sql = "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                        "x<100000000) SELECT sum(x) FROM n";
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    editor->setText(sql);
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
    run->trigger();
    QTRY_VERIFY(running);
    bool confirmed = false;
    QTimer chooser;
    chooser.setInterval(5);
    connect(&chooser, &QTimer::timeout, &window, [&] {
        for (auto* widget : {choscordb::design::DialogPresentation::activeDialog()})
            if (auto* box = qobject_cast<QMessageBox*>(widget))
                for (auto* button : box->buttons())
                    if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
                        confirmed = true;
                        chooser.stop();
                        button->click();
                        return;
                    }
    });
    chooser.start();
    window.close();
    QTRY_VERIFY(!window.isVisible());
    QVERIFY(confirmed);
    choscordb::EngineAdapter reopened(nullptr, path);
    QSignalSpy entries(&reopened, &choscordb::EngineAdapter::historyListed);
    QVERIFY(reopened.listHistory(100, 0, 818));
    QTRY_COMPARE(entries.count(), 1);
    const auto saved = qvariant_cast<QList<choscordb::SavedHistoryEntry>>(entries.at(0).at(1));
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved[0].sql, sql);
    QCOMPARE(saved[0].status, QString("disconnected"));
    QVERIFY(!saved[0].hasRowCount);
}

void WorkspaceTest::mainWindowSearchActionsTrackCurrentTabAndUndo() {
    choscordb::MainWindow window;
    window.show();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* first = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    const QString original = QString::fromUtf8("SELECT 'é cat';\nSELECT 'cat';");
    first->setText(original);
    auto trigger = [&](QKeySequence shortcut) {
        for (auto* action : window.findChildren<QAction*>())
            if (action->shortcut() == shortcut) {
                action->trigger();
                return;
            }
        QFAIL("Expected menu shortcut action");
    };
    trigger(QKeySequence::Replace);
    auto* panel = window.findChild<choscordb::SearchPanel*>();
    QVERIFY(panel->isVisible());
    auto* needle = panel->findChild<QLineEdit*>("searchNeedle");
    auto* replacement = panel->findChild<QLineEdit*>("searchReplacement");
    needle->setText("cat");
    replacement->setText("dog");
    panel->findChild<QPushButton*>("searchReplaceAll")->click();
    QTRY_COMPARE(first->text(), QString::fromUtf8("SELECT 'é dog';\nSELECT 'dog';"));
    QVERIFY(window.grab().save("native-search.png"));
    first->undo();
    QCOMPARE(first->text(), original);
    trigger(QKeySequence::Find);
    trigger(QKeySequence::FindNext);
    QTRY_COMPARE(first->selectedText(), QString("cat"));
    trigger(QKeySequence::New);
    QCOMPARE(tabs->count(), 2);
    auto* second = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    second->setText("SELECT 'cat';");
    trigger(QKeySequence::Replace);
    auto* replace = panel->findChild<QPushButton*>("searchReplace");
    replace->click();
    QTRY_COMPARE(second->selectedText(), QString("cat"));
    QCOMPARE(second->text(), QString("SELECT 'cat';"));
    replace->click();
    QCOMPARE(second->text(), QString("SELECT 'dog';"));
    QCOMPARE(first->text(), original);
    needle->setFocus();
    QTest::keyClick(needle, Qt::Key_Escape);
    QTRY_VERIFY(!panel->isVisible());
}

void WorkspaceTest::mainWindowRunsRealQuery() {
    choscordb::MainWindow window;
    window.resize(1280, 900);
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QVERIFY(workspace);
    auto* editor = window.findChild<choscordb::SqlEditor*>();
    QVERIFY(editor);
    auto* grid = window.findChild<QTableView*>("queryResults");
    QVERIFY(grid);
    auto* run = window.findChild<QAction*>("runStatement");
    QVERIFY(run);
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(run->isEnabled());
    editor->setText("SELECT 3 AS result");
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
    run->trigger();
    QTRY_COMPARE(grid->model()->rowCount(), 1);
    QCOMPARE(grid->model()->data(grid->model()->index(0, 0)).toString(), QString("3"));
    QTest::qWait(30);
    QVERIFY(window.grab().save("native-workspace.png"));
    workspace->shutdown();
}

QTEST_MAIN(WorkspaceTest)
