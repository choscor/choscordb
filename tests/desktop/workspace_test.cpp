#include "app/main_window.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "models/history_model.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/history_dock/history_dock.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/search_panel/search_panel.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
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

struct WorkspaceFixture {
    QWidget parent;
    QComboBox connections, mode;
    QAction run, cancel, commit, rollback, newConnection;
    QPushButton next, previous, exportResult;
    QLabel summary;
    QPlainTextEdit messages;
    QTableView grid;
    choscordb::SqlEditor editor;
    choscordb::QueryWorkspace workspace;
    WorkspaceFixture()
        : workspace({&connections,
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
                     &parent,
                     &previous,
                     &exportResult,
                     {}}) {
        mode.addItems({"Auto-commit", "Manual"});
        connections.setEnabled(false);
        mode.setEnabled(false);
        workspace.connectSqlite(":memory:");
    }
    void execute(const QString& sql) {
        QTRY_VERIFY(run.isEnabled());
        QVERIFY2(!choscordb::EngineAdapter::executionRange(sql, 0, 0, 0).confirmation,
                 "Test SQL must not trigger confirmation");
        editor.setText(sql);
        editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        run.trigger();
    }
};
class WorkspaceTest : public QObject {
    Q_OBJECT
  private slots:
    void newConnectionAfterSavingCreatesAnotherProfile() {
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
    void cancellationRacingSuccessfulCompletionReleasesNavigation() {
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
    void immediateCancellationDoesNotReenableCancelBeforeAcknowledgement() {
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
    void connectionPanelRetainsFailedSaveConnectDraftAndRetries() {
        QTemporaryDir directory;
        WorkspaceFixture f;
        f.parent.resize(960, 640);
        f.parent.show();
        QTRY_VERIFY(f.run.isEnabled());
        f.newConnection.trigger();
        auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>();
        QVERIFY(dialog);
        QVERIFY(dialog->isModal());
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
    }
    void saveAndConnectPersistsProfileBeforeOpeningSession() {
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
    void recoveryAdapterRejectsOversizeBeforeDispatch() {
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
        const auto result =
            qvariant_cast<QList<choscordb::SavedEditorDocument>>(restored.at(0).at(1));
        QCOMPARE(result.size(), 1);
        QCOMPARE(result[0].sql, QString("SELECT 'é';"));
        QCOMPARE(result[0].cursorOffset, quint64(10));
        QCOMPARE(result[0].selectionAnchor, quint64(8));
        QVERIFY(result[0].modified);
    }
    void sqlStartedTransactionRequiresCloseConfirmation() {
        WorkspaceFixture f;
        QTRY_VERIFY(f.run.isEnabled());
        const QString begin = "BEGIN TRANSACTION";
        if (choscordb::EngineAdapter::executionRange(begin, 0, 0, 0).confirmation)
            QTimer::singleShot(0, &f.parent, [] {
                for (auto* widget : QApplication::topLevelWidgets())
                    if (auto* box = qobject_cast<QMessageBox*>(widget))
                        box->button(QMessageBox::Yes)->click();
            });
        f.editor.setText(begin);
        f.run.trigger();
        QTRY_VERIFY(f.messages.toPlainText().contains("Completed"));
        bool asked = false;
        QTimer::singleShot(0, &f.parent, [&] {
            for (auto* widget : QApplication::topLevelWidgets())
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
                for (auto* widget : QApplication::topLevelWidgets())
                    if (auto* box = qobject_cast<QMessageBox*>(widget))
                        box->button(QMessageBox::Yes)->click();
            });
        f.run.trigger();
        QTRY_VERIFY(f.messages.toPlainText().contains("Completed"));
        QVERIFY(f.workspace.confirmShutdown());
    }
    void transactionCloseRequiresExplicitChoice() {
        WorkspaceFixture f;
        QTRY_VERIFY(f.run.isEnabled());
        f.mode.setCurrentIndex(1);
        f.execute("SELECT 1");
        QTRY_VERIFY(f.run.isEnabled());
        bool cancelled = false;
        QTimer::singleShot(0, &f.parent, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            cancelled = true;
            box->button(QMessageBox::Cancel)->click();
        });
        QVERIFY(!f.workspace.confirmShutdown());
        QVERIFY(cancelled);
        QVERIFY(f.run.isEnabled());
        bool approved = false;
        QTimer::singleShot(0, &f.parent, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            for (auto* button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
                    approved = true;
                    button->click();
                    return;
                }
            QFAIL("missing close action");
        });
        QVERIFY(f.workspace.confirmShutdown());
        QVERIFY(approved);
        f.rollback.trigger();
        QTRY_VERIFY(f.messages.toPlainText().contains("rolled back"));
        QVERIFY(f.workspace.confirmShutdown());
    }
    void mainWindowHistoryRecordsOpensDisablesAndFlushes() {
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
        connect(adapter, &choscordb::EngineAdapter::eventReady, &window,
                [&](const choscordb::BridgeEvent& event) {
                    if (QString::fromUtf8(event.kind.data(),
                                          static_cast<qsizetype>(event.kind.size())) ==
                        "query_finished")
                        ++finished;
                });
        workspace->connectSqlite(":memory:");
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
        auto* record = history->findChild<QCheckBox*>("recordHistory");
        QTRY_VERIFY(record->isEnabled());
        record->click();
        QTRY_VERIFY(record->isEnabled());
        QVERIFY(!record->isChecked());
        executeSql("SELECT 3");
        history->refresh();
        QTRY_VERIFY(history->findChild<QPushButton*>("refreshHistory")->isEnabled());
        QCOMPARE(model->rowCount(), 1);
        QTimer::singleShot(0, &window, [] {
            for (auto* widget : QApplication::topLevelWidgets())
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
        const auto persisted =
            qvariant_cast<QList<choscordb::SavedHistoryEntry>>(entries.at(0).at(1));
        QCOMPARE(persisted.size(), 1);
        QCOMPARE(persisted[0].sql, QString("SELECT 4"));
    }
    void closingActiveQueryFlushesDisconnectedHistory() {
        QTemporaryDir directory;
        const auto path = directory.filePath("metadata.sqlite");
        choscordb::MainWindow window(nullptr, path);
        window.show();
        auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
        QTRY_VERIFY(recovery->isReady());
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        auto* run = window.findChild<QAction*>("runStatement");
        workspace->connectSqlite(":memory:");
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
            for (auto* widget : QApplication::topLevelWidgets())
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
    void mainWindowSearchActionsTrackCurrentTabAndUndo() {
        choscordb::MainWindow window;
        window.show();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
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
    void mainWindowRunsRealQuery() {
        choscordb::MainWindow window;
        window.resize(1280, 900);
        window.show();
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
    void connectsExecutesPagesAndCopies() {
        WorkspaceFixture f;
        QTRY_COMPARE(f.connections.count(), 1);
        QVERIFY(f.connections.isEnabled());
        QVERIFY(f.mode.isEnabled());
        QVERIFY(f.run.isEnabled());
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
    void revisitsPreviousPageWithoutReexecutingQuery() {
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
    void byteLimitedPagesUseStoredRowOffsets() {
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
    void transactionKeepsAlreadyStoredNextPageAccessible() {
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
    void disconnectClearsPageNavigation() {
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
    void visiblePageRemainsBudgetedAfterCoreQueryRelease() {
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
    void repeatedPagingReleasesReplacedViews() {
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
    void nestedEventLoopKeepsLiveTransferReserved() {
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
    void previousPageUsesSharedHotCache() {
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
    void deferredCellOpensBoundedDetailAndClosesOnNewQuery() {
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
    void deferredTextRemainsInspectableAfterCommitAndDisconnectClosesDetail() {
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
        QTRY_COMPARE(preview->model()->data(preview->model()->index(0, 0)).toULongLong(),
                     quint64(0));
        f.workspace.adapter()->disconnectConnection(f.connections.currentData().toULongLong());
        QTRY_COMPARE(f.connections.count(), 0);
        QVERIFY(!dialog->isVisible());
        QCOMPARE(preview->model()->rowCount(), 0);
    }
    void backwardTextWindowAlignsUtf8AndLongRowsScroll() {
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
    void exportsOriginalResultAfterBrowsing() {
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
    void exportCancellationPreservesExistingDestination() {
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
            for (auto* widget : QApplication::topLevelWidgets())
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
        QTRY_COMPARE(QDir(directory.path())
                         .entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)
                         .size(),
                     1);
    }
    void exportFailureLeavesResultUsable() {
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
    void savedProfilesCreateDuplicateTestDeleteAndConnect() {
        WorkspaceFixture f;
        QTRY_COMPARE(f.connections.count(), 1);
        f.newConnection.trigger();
        auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
        QVERIFY(dialog);
        auto* save = dialog->findChild<QPushButton*>("profileSave");
        auto* list = dialog->findChild<QListWidget*>("profileList");
        auto* status = dialog->findChild<QLabel*>("profileStatus");
        QVERIFY(save && list && status);
        QTRY_VERIFY(save->isEnabled());
        choscordb::SavedProfile profile;
        profile.id = "native-local";
        profile.name = "Saved SQLite";
        profile.path = ":memory:";
        dialog->saveDraft(profile);
        QTRY_COMPARE(list->count(), 1);
        QTRY_VERIFY(save->isEnabled());
        dialog->testDraft(profile);
        QTRY_VERIFY(status->text().contains("succeeded"));
        QCOMPARE(f.connections.count(), 1);
        dialog->findChild<QPushButton*>("profileDuplicate")->click();
        QTRY_COMPARE(list->count(), 2);
        QTRY_VERIFY(save->isEnabled());
        QTimer::singleShot(0, dialog, [] {
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* box = qobject_cast<QMessageBox*>(widget))
                    box->done(QMessageBox::Yes);
        });
        dialog->findChild<QPushButton*>("profileDelete")->click();
        QTRY_COMPARE(list->count(), 1);
        dialog->selectProfile(profile.id);
        dialog->findChild<QPushButton*>("profileConnect")->click();
        QTRY_COMPARE(f.connections.count(), 2);
        // Connecting another session leaves this draft's existing target intact.
        QCOMPARE(f.connections.currentIndex(), 0);
        f.connections.setCurrentIndex(1);
        QCOMPARE(f.connections.currentText(), QString("Saved SQLite"));
        QVERIFY(dialog->grab().save("native-profiles.png"));
        f.execute("SELECT 7");
        QTRY_VERIFY(f.run.isEnabled());
        QCOMPARE(f.editor.property("profileId").toString(), profile.id);
        f.connections.setCurrentIndex(0);
        f.messages.clear();
        f.execute("SELECT 8");
        QTRY_VERIFY(f.run.isEnabled());
        QVERIFY(f.editor.property("profileId").toString().isEmpty());
        QTRY_VERIFY(f.messages.toPlainText().contains("Completed"));
        QSignalSpy history(f.workspace.adapter(), &choscordb::EngineAdapter::historyListed);
        QVERIFY(f.workspace.adapter()->listHistory(100, 0, 777));
        QTRY_COMPARE(history.count(), 1);
        const auto entries =
            qvariant_cast<QList<choscordb::SavedHistoryEntry>>(history.at(0).at(1));
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].sql, QString("SELECT 8"));
        QVERIFY(entries[0].profileId.isEmpty());
        QCOMPARE(entries[1].sql, QString("SELECT 7"));
        QCOMPARE(entries[1].profileId, profile.id);
    }
    void profileAdapterPersistsAcrossRestart() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto storagePath = directory.filePath("metadata.sqlite");
        {
            choscordb::EngineAdapter adapter(nullptr, storagePath);
            bool saved = false;
            connect(
                &adapter, &choscordb::EngineAdapter::profileSaved, &adapter,
                [&saved](quint64 token, const choscordb::SavedProfile&) { saved = token == 77; });
            choscordb::SavedProfile profile;
            profile.id = "persisted";
            profile.name = "Persistent SQLite";
            profile.path = ":memory:";
            adapter.saveProfile(profile, 77);
            QTRY_VERIFY(saved);
        }
        choscordb::EngineAdapter adapter(nullptr, storagePath);
        QList<choscordb::SavedProfile> profiles;
        bool listed = false;
        connect(&adapter, &choscordb::EngineAdapter::profilesReady, &adapter,
                [&](quint64 token, const QList<choscordb::SavedProfile>& result) {
                    if (token == 78) {
                        profiles = result;
                        listed = true;
                    }
                });
        adapter.listProfiles(78);
        QTRY_VERIFY(listed);
        QCOMPARE(profiles.size(), 1);
        QCOMPARE(profiles[0].name, QString("Persistent SQLite"));
    }
    void profileFailuresKeepDraftAndShowConnectionCodes() {
        WorkspaceFixture f;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QTRY_COMPARE(f.connections.count(), 1);
        f.newConnection.trigger();
        auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
        QVERIFY(dialog);
        auto* save = dialog->findChild<QPushButton*>("profileSave");
        auto* status = dialog->findChild<QLabel*>("profileStatus");
        auto* name = dialog->findChild<QLineEdit*>("profileName");
        QVERIFY(save && status && name);
        QTRY_VERIFY(save->isEnabled());
        choscordb::SavedProfile profile;
        profile.id = "invalid-path";
        profile.name = "Unsaved draft";
        dialog->saveDraft(profile);
        QTRY_VERIFY(save->isEnabled());
        QCOMPARE(name->text(), QString("Unsaved draft"));
        QVERIFY(status->text().contains("Invalid", Qt::CaseInsensitive));
        profile.path = directory.filePath("missing/data.sqlite");
        dialog->testDraft(profile);
        QTRY_VERIFY(status->text().contains("[Code: 14]"));
        QCOMPARE(f.connections.count(), 1);
        dialog->saveDraft(profile);
        QTRY_VERIFY(save->isEnabled());
        auto* open = dialog->findChild<QPushButton*>("profileConnect");
        QVERIFY(open);
        open->click();
        QVERIFY(!open->isEnabled());
        QTRY_VERIFY(open->isEnabled());
        QVERIFY(status->text().contains("[Code: 14]"));
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QCOMPARE(f.connections.count(), 1);
    }
    void passwordDraftSurvivesTestConnectAndFailedRemember() {
        WorkspaceFixture f;
        QTRY_COMPARE(f.connections.count(), 1);
        f.newConnection.trigger();
        auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
        QVERIFY(dialog);
        auto* save = dialog->findChild<QPushButton*>("profileSave");
        auto* password = dialog->findChild<QLineEdit*>("profilePassword");
        auto* remember = dialog->findChild<QCheckBox*>("profileRememberPassword");
        auto* status = dialog->findChild<QLabel*>("profileStatus");
        QVERIFY(save && password && remember && status);
        QTRY_VERIFY(save->isEnabled());
        choscordb::SavedProfile profile;
        profile.id = "password-draft";
        profile.name = "PostgreSQL draft";
        profile.driver = "postgres";
        profile.host = "localhost";
        profile.database = "app";
        profile.user = "user";
        profile.port = 1;
        dialog->saveDraft(profile);
        QTRY_VERIFY(save->isEnabled());
        QCOMPARE(password->echoMode(), QLineEdit::Password);
        password->setText("draft-secret");
        password->setModified(true);
        dialog->testDraft(profile);
        QTRY_VERIFY(save->isEnabled());
        QCOMPARE(password->text(), QString("draft-secret"));
        QVERIFY(password->isModified());
        dialog->findChild<QPushButton*>("profileConnect")->click();
        QTRY_VERIFY(save->isEnabled());
        QCOMPARE(password->text(), QString("draft-secret"));
        dialog->saveDraft(profile);
        QTRY_VERIFY(save->isEnabled());
        QCOMPARE(password->text(), QString("draft-secret"));
        remember->setChecked(true);
        dialog->saveDraft(profile);
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(status->text().contains("unavailable", Qt::CaseInsensitive));
        QCOMPARE(password->text(), QString("draft-secret"));
        QVERIFY(!status->text().contains("draft-secret"));
        QVERIFY(dialog->grab().save("native-profile-password.png"));
    }
    void postgresProfileExecutesPagesAndCancels() {
        if (qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_PORT").isEmpty())
            QSKIP("Requires the isolated PostgreSQL fixture environment");
        WorkspaceFixture f;
        QTRY_COMPARE(f.connections.count(), 1);
        f.newConnection.trigger();
        auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
        QVERIFY(dialog);
        auto* save = dialog->findChild<QPushButton*>("profileSave");
        auto* password = dialog->findChild<QLineEdit*>("profilePassword");
        auto* status = dialog->findChild<QLabel*>("profileStatus");
        QVERIFY(save && password && status);
        QTRY_VERIFY(save->isEnabled());
        choscordb::SavedProfile profile;
        profile.id = "postgres-live";
        profile.name = "PostgreSQL integration";
        profile.driver = "postgres";
        profile.host = qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_HOST", "localhost");
        profile.port = qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_PORT").toUShort();
        profile.database = qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_DATABASE", "postgres");
        profile.user = qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_USER", "choscordb");
        profile.rootCertificate = qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE");
        profile.tls = "verify_full";
        dialog->saveDraft(profile);
        QTRY_VERIFY(save->isEnabled());
        password->setText(qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_PASSWORD"));
        password->setModified(true);
        dialog->testDraft(profile);
        QTRY_VERIFY2(status->text().contains("succeeded"), qPrintable(status->text()));
        QCOMPARE(f.connections.count(), 1);
        dialog->findChild<QPushButton*>("profileConnect")->click();
        QTRY_COMPARE(f.connections.count(), 2);
        f.connections.setCurrentIndex(1);
        f.execute("SELECT i::numeric(30,8) AS value FROM generate_series(1,1001) AS i");
        QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
        QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(),
                 QString("1.00000000"));
        f.next.click();
        QTRY_COMPARE(f.grid.model()->rowCount(), 1);
        QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(),
                 QString("1001.00000000"));
        f.previous.click();
        QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
        f.execute("SELECT pg_sleep(30)");
        QTRY_VERIFY(f.cancel.isEnabled());
        f.cancel.trigger();
        QTRY_VERIFY(f.run.isEnabled());
        QTRY_VERIFY2(f.messages.toPlainText().contains("cancel", Qt::CaseInsensitive),
                     qPrintable(f.messages.toPlainText()));
        f.messages.clear();
        f.execute("SELECT 7::bigint AS recovered");
        QTRY_COMPARE(f.grid.model()->rowCount(), 1);
        QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("7"));
        QTRY_VERIFY(f.messages.toPlainText().contains("Completed"));
        QSignalSpy history(f.workspace.adapter(), &choscordb::EngineAdapter::historyListed);
        QVERIFY(f.workspace.adapter()->listHistory(100, 0, 919));
        QTRY_COMPARE(history.count(), 1);
        const auto entries =
            qvariant_cast<QList<choscordb::SavedHistoryEntry>>(history.at(0).at(1));
        QCOMPARE(entries.size(), 3);
        for (const auto& entry : entries)
            QCOMPARE(entry.profileId, profile.id);
        QCOMPARE(entries[0].sql, QString("SELECT 7::bigint AS recovered"));
        QCOMPARE(entries[0].rowCount, quint64(1));
        QCOMPARE(entries[1].status, QString("cancelled"));
        QVERIFY(!entries[1].hasRowCount);
        QCOMPARE(entries[2].rowCount, quint64(1001));
        QVERIFY(entries[2].hasRowCount);
    }
    void writesCommitAndRollbackComplete() {
        WorkspaceFixture f;
        QTRY_VERIFY(f.run.isEnabled());
        f.execute("CREATE TABLE t(x INTEGER)");
        QTRY_VERIFY(f.messages.toPlainText().contains("rows affected"));
        QTRY_VERIFY(f.run.isEnabled());
        f.mode.setCurrentIndex(1);
        f.messages.clear();
        f.execute("INSERT INTO t VALUES(1)");
        QTRY_VERIFY(f.messages.toPlainText().contains("1 rows affected"));
        QTRY_VERIFY(f.rollback.isEnabled());
        f.mode.setCurrentIndex(0);
        QCOMPARE(f.mode.currentIndex(), 1);
        QVERIFY(f.messages.toPlainText().contains("Commit or roll back"));
        f.rollback.trigger();
        QTRY_VERIFY(f.messages.toPlainText().contains("rolled back"));
        f.messages.clear();
        f.execute("INSERT INTO t VALUES(2)");
        QTRY_VERIFY(f.messages.toPlainText().contains("1 rows affected"));
        f.commit.trigger();
        QTRY_VERIFY(f.messages.toPlainText().contains("committed"));
        f.execute("SELECT x FROM t");
        QTRY_COMPARE(f.grid.model()->rowCount(), 1);
        QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("2"));
    }
    void cancelsRunningQuery() {
        WorkspaceFixture f;
        QTRY_VERIFY(f.run.isEnabled());
        f.execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<100000000) "
                  "SELECT sum(x) FROM n");
        QTRY_VERIFY(f.cancel.isEnabled());
        f.cancel.trigger();
        QTRY_VERIFY(f.run.isEnabled());
        QVERIFY(f.grid.model()->rowCount() == 0);
    }
};
QTEST_MAIN(WorkspaceTest)
#include "workspace_test.moc"
