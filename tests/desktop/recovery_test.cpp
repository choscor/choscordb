#include "app/main_window.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <QtTest>
using namespace choscordb;
class RecoveryTest : public QObject {
    Q_OBJECT
  private slots:
    void actualMainWindowRestoresDisconnectedAfterRestart() {
        QTemporaryDir directory;
        const auto path = directory.filePath("metadata.sqlite");
        {
            EngineAdapter adapter(nullptr, path);
            QSignalSpy saved(&adapter, &EngineAdapter::workspaceSaved);
            SavedEditorDocument document;
            document.id = "restarted";
            document.title = "Recovered Unicode";
            document.sql = QString::fromUtf8("SELECT 'é';");
            document.filePath = "/missing/recovered.sql";
            document.profileId = "absent";
            document.cursorOffset = 10;
            document.selectionAnchor = 8;
            document.modified = true;
            QVERIFY(adapter.saveWorkspace({document}, 1));
            QTRY_COMPARE(saved.count(), 1);
        }
        MainWindow window(nullptr, path);
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 0);
        QApplication::clipboard()->setText("injected paste");
        for (auto* action : window.findChildren<QAction*>())
            if (action->shortcut() == QKeySequence::Paste)
                action->trigger();
        QCOMPARE(tabs->count(), 0);
        for (auto* action : window.findChildren<QAction*>())
            if (action->shortcut() == QKeySequence::Replace ||
                action->shortcut() == QKeySequence::Find)
                QVERIFY(!action->isEnabled());
        auto* controller = window.findChild<WorkspaceRecoveryController*>();
        QVERIFY(controller);
        controller->start();
        QTRY_VERIFY(controller->isReady());
        const auto documents = controller->snapshot();
        QCOMPARE(documents.size(), 1);
        QCOMPARE(documents.at(0).id, QString("restarted"));
        QCOMPARE(documents.at(0).sql, QString::fromUtf8("SELECT 'é';"));
        QCOMPARE(documents.at(0).filePath, QString("/missing/recovered.sql"));
        QCOMPARE(documents.at(0).profileId, QString("absent"));
        QCOMPARE(documents.at(0).cursorOffset, quint64(10));
        QCOMPARE(documents.at(0).selectionAnchor, quint64(8));
        QVERIFY(documents.at(0).modified);
        auto* connections = window.findChild<QComboBox*>("connectionSelector");
        QVERIFY(connections);
        QCOMPARE(connections->count(), 1);
        QVERIFY(!connections->currentData().isValid());
        auto* editor = qobject_cast<SqlEditor*>(tabs->currentWidget());
        QVERIFY(editor);
        const auto updatedSql = QString::fromUtf8("SELECT 'é';\nSELECT 'saved at close';");
        editor->setText(updatedSql);
        editor->setModified(true);
        QSignalSpy persisted(controller, &WorkspaceRecoveryController::persistenceSucceeded);
        controller->flush();
        QTRY_VERIFY(persisted.count() >= 1);
        MainWindow restarted(nullptr, path);
        auto* restoredController = restarted.findChild<WorkspaceRecoveryController*>();
        QVERIFY(restoredController);
        restoredController->start();
        QTRY_VERIFY(restoredController->isReady());
        const auto restoredDocuments = restoredController->snapshot();
        QCOMPARE(restoredDocuments.size(), 1);
        QCOMPARE(restoredDocuments[0].sql, updatedSql);
        QCOMPARE(restoredDocuments[0].profileId, QString("absent"));
        QCOMPARE(restoredDocuments[0].filePath, QString("/missing/recovered.sql"));
        QVERIFY(restoredDocuments[0].modified);
    }
    void mixedWorkspaceRestoresObjectAndSqlWithoutConnecting() {
        QTemporaryDir directory;
        const auto path = directory.filePath("mixed.sqlite");
        SavedWorkspaceTab object;
        object.isObject = true;
        object.profileId = "profile:missing-profile";
        object.objectType = "table";
        object.objectId = "public.orders";
        object.label = "orders";
        object.pane = 3;
        SavedWorkspaceTab sql;
        sql.document.id = "draft";
        sql.document.title = "Draft";
        sql.document.sql = "SELECT 17;";
        sql.document.cursorOffset = 10;
        sql.document.selectionAnchor = 10;
        sql.document.modified = true;
        {
            EngineAdapter adapter(nullptr, path);
            QSignalSpy saved(&adapter, &EngineAdapter::workspaceSaved);
            QVERIFY(adapter.saveWorkspaceTabs({object, sql}, 0, 1));
            QTRY_COMPARE(saved.count(), 1);
        }
        MainWindow window(nullptr, path);
        auto* controller = window.findChild<WorkspaceRecoveryController*>();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        QVERIFY(controller);
        QVERIFY(tabs);
        controller->start();
        QTRY_VERIFY(controller->isReady());
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->currentIndex(), 0);
        auto* explorer = qobject_cast<ObjectExplorer*>(tabs->widget(0));
        QVERIFY(explorer);
        QCOMPARE(explorer->property("objectProfileId").toString(), object.profileId);
        QCOMPARE(explorer->property("objectId").toString(), object.objectId);
        QCOMPARE(explorer->paneIndex(), int(object.pane));
        QVERIFY(explorer->needsConnection());
        auto* workspace = window.findChild<QueryWorkspace*>();
        QSignalSpy unexpectedReads(workspace->adapter(), &EngineAdapter::objectInspectionReady);
        QSignalSpy unexpectedFailures(workspace->adapter(), &EngineAdapter::objectInspectionFailed);
        QTest::qWait(30);
        QCOMPARE(unexpectedReads.count(), 0);
        QCOMPARE(unexpectedFailures.count(), 0);
        SavedProfile profile;
        profile.id = "missing-profile";
        profile.name = "Recovered connection";
        profile.path = ":memory:";
        workspace->adapter()->saveProfile(profile, 45);
        auto* savedConnections = window.findChild<QListWidget*>("savedConnections");
        QTRY_COMPARE(savedConnections->count(), 1);
        auto* reconnect = explorer->findChild<QPushButton*>("objectReconnect");
        QVERIFY(reconnect);
        reconnect->click();
        QTRY_VERIFY(!explorer->needsConnection());
        auto* editor = qobject_cast<SqlEditor*>(tabs->widget(1));
        QVERIFY(editor);
        QCOMPARE(editor->text(), sql.document.sql);
        QVERIFY(editor->isModified());
        QSignalSpy persisted(controller, &WorkspaceRecoveryController::persistenceSucceeded);
        tabs->setCurrentIndex(1);
        controller->flush();
        QTRY_VERIFY(persisted.count() >= 1);
        MainWindow restarted(nullptr, path);
        auto* restored = restarted.findChild<WorkspaceRecoveryController*>();
        auto* restoredTabs = restarted.findChild<QTabWidget*>("editorTabs");
        QVERIFY(restored);
        QVERIFY(restoredTabs);
        restored->start();
        QTRY_VERIFY(restored->isReady());
        QCOMPARE(restoredTabs->count(), 2);
        QCOMPARE(restoredTabs->currentIndex(), 1);
        QCOMPARE(qobject_cast<ObjectExplorer*>(restoredTabs->widget(0))->paneIndex(),
                 int(object.pane));
        QCOMPARE(qobject_cast<SqlEditor*>(restoredTabs->widget(1))->text(), sql.document.sql);
        QSignalSpy persistedAgain(restored, &WorkspaceRecoveryController::persistenceSucceeded);
        restoredTabs->setCurrentIndex(0);
        restored->flush();
        QTRY_VERIFY(persistedAgain.count() >= 1);
        MainWindow objectActive(nullptr, path);
        auto* finalRecovery = objectActive.findChild<WorkspaceRecoveryController*>();
        auto* finalTabs = objectActive.findChild<QTabWidget*>("editorTabs");
        QVERIFY(finalRecovery);
        QVERIFY(finalTabs);
        finalRecovery->start();
        QTRY_VERIFY(finalRecovery->isReady());
        QCOMPARE(finalTabs->count(), 2);
        QCOMPARE(finalTabs->currentIndex(), 0);
        QCOMPARE(qobject_cast<ObjectExplorer*>(finalTabs->widget(0))->paneIndex(),
                 int(object.pane));
    }

    void pendingFileReadDefersCloseUntilLatestBufferCanBeSaved() {
        QTemporaryDir dir;
        QFile file(dir.filePath("pending.sql"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("SELECT 'loaded';");
        file.close();
        auto* pool = QThreadPool::globalInstance();
        pool->waitForDone();
        const int previous = pool->maxThreadCount();
        pool->setMaxThreadCount(1);
        QSemaphore started, release;
        auto blocker = QtConcurrent::run([&] {
            started.release();
            release.acquire();
        });
        auto cleanup = qScopeGuard([&] {
            release.release();
            blocker.waitForFinished();
            pool->waitForDone();
            pool->setMaxThreadCount(previous);
        });
        QVERIFY(started.tryAcquire(1, 5000));
        QTabWidget tabs;
        auto add = [&] {
            auto* e = new SqlEditor;
            tabs.addTab(e, "Untitled");
            return e;
        };
        add();
        WorkspaceRecoveryController recovery(&tabs, add);
        QSignalSpy restores(&recovery, &WorkspaceRecoveryController::restoreRequested);
        QSignalSpy saves(&recovery, &WorkspaceRecoveryController::saveRequested);
        QSignalSpy errors(&recovery, &WorkspaceRecoveryController::errorOccurred);
        recovery.start();
        recovery.restored(restores.at(0).at(0).toULongLong(), {});
        auto* editor = qobject_cast<SqlEditor*>(tabs.widget(0));
        editor->openFile(file.fileName());
        QVERIFY(editor->isIoBusy());
        recovery.requestClose();
        QCoreApplication::processEvents();
        QCOMPARE(saves.count(), 0);
        QCOMPARE(errors.count(), 0);
        release.release();
        QTRY_COMPARE(saves.count(), 1);
        const auto documents = qvariant_cast<QList<SavedEditorDocument>>(saves.at(0).at(0));
        QCOMPARE(documents.at(0).sql, QString("SELECT 'loaded';"));
        QCOMPARE(documents.at(0).filePath, file.fileName());
        QVERIFY(!documents.at(0).modified);
    }
    void closeDuringStartupWaitsWithoutPrematureError() {
        QTabWidget tabs;
        auto add = [&] {
            auto* e = new SqlEditor;
            tabs.addTab(e, "Untitled");
            return e;
        };
        add();
        WorkspaceRecoveryController recovery(&tabs, add);
        QSignalSpy restores(&recovery, &WorkspaceRecoveryController::restoreRequested);
        QSignalSpy saves(&recovery, &WorkspaceRecoveryController::saveRequested);
        QSignalSpy errors(&recovery, &WorkspaceRecoveryController::errorOccurred);
        QSignalSpy closed(&recovery, &WorkspaceRecoveryController::closeReady);
        recovery.start();
        recovery.requestClose();
        QCOMPARE(errors.count(), 0);
        QCOMPARE(closed.count(), 0);
        recovery.restored(restores.at(0).at(0).toULongLong(), {});
        QVERIFY(!tabs.isEnabled());
        QCOMPARE(saves.count(), 1);
        recovery.saved(saves.at(0).at(1).toULongLong());
        QTRY_COMPARE(closed.count(), 1);
    }
    void invalidRestoreIsAtomicAndCancelledCloseDoesNotEmitReady() {
        QTabWidget tabs;
        auto add = [&] {
            auto* e = new SqlEditor;
            tabs.addTab(e, "Untitled");
            return e;
        };
        auto* original = add();
        original->setText("original");
        WorkspaceRecoveryController recovery(&tabs, add);
        QSignalSpy restores(&recovery, &WorkspaceRecoveryController::restoreRequested);
        QSignalSpy saves(&recovery, &WorkspaceRecoveryController::saveRequested);
        QSignalSpy closed(&recovery, &WorkspaceRecoveryController::closeReady);
        QSignalSpy errors(&recovery, &WorkspaceRecoveryController::errorOccurred);
        recovery.start();
        SavedEditorDocument invalid;
        invalid.id = "same";
        invalid.title = "T";
        invalid.sql = "SELECT 1";
        recovery.restored(restores.at(0).at(0).toULongLong(), {invalid, invalid});
        QCOMPARE(errors.count(), 1);
        QCOMPARE(tabs.widget(0), original);
        QCOMPARE(original->text(), QString("original"));
        recovery.retry();
        recovery.restored(restores.at(1).at(0).toULongLong(), {});
        recovery.requestClose();
        QCOMPARE(saves.count(), 1);
        recovery.saved(saves.at(0).at(1).toULongLong());
        recovery.cancelClose();
        QCoreApplication::processEvents();
        QCOMPARE(closed.count(), 0);
        QVERIFY(tabs.isEnabled());
    }
    void restoresDisconnectedUnicodeAndCoalescesSaves() {
        QTabWidget tabs;
        auto add = [&] {
            auto* e = new SqlEditor;
            tabs.addTab(e, "Untitled");
            return e;
        };
        add();
        WorkspaceRecoveryController recovery(&tabs, add);
        QSignalSpy restores(&recovery, &WorkspaceRecoveryController::restoreRequested);
        QSignalSpy saves(&recovery, &WorkspaceRecoveryController::saveRequested);
        QSignalSpy closed(&recovery, &WorkspaceRecoveryController::closeReady);
        recovery.start();
        QCOMPARE(restores.count(), 1);
        QVERIFY(!tabs.isEnabled());
        const auto token = restores.at(0).at(0).toULongLong();
        SavedEditorDocument doc;
        doc.id = "stable";
        doc.title = "Recovered";
        doc.sql = QString::fromUtf8("SELECT 'é';");
        doc.filePath = "/does/not/exist.sql";
        doc.profileId = "saved-profile";
        doc.cursorOffset = 10;
        doc.selectionAnchor = 8;
        doc.modified = true;
        auto second = doc;
        second.id = "second";
        second.title = "Second";
        second.sql = "SELECT 2;";
        second.cursorOffset = 0;
        second.selectionAnchor = 0;
        recovery.restored(token, {doc, second});
        QVERIFY(tabs.isEnabled());
        auto* e = qobject_cast<SqlEditor*>(tabs.widget(0));
        QCOMPARE(e->text(), doc.sql);
        QCOMPARE(e->filePath(), doc.filePath);
        QVERIFY(e->isModified());
        QCOMPARE(recovery.snapshot().size(), 2);
        QCOMPARE(recovery.snapshot().at(0).id, doc.id);
        QCOMPARE(recovery.snapshot().at(1).id, second.id);
        QCOMPARE(recovery.snapshot().at(0).selectionAnchor, quint64(8));
        e->append(" -- edit");
        recovery.flush();
        QCOMPARE(saves.count(), 1);
        e->append(" more");
        recovery.flush();
        QCOMPARE(saves.count(), 1);
        recovery.requestClose();
        QCOMPARE(closed.count(), 0);
        recovery.saved(saves.at(0).at(1).toULongLong());
        QTRY_COMPARE(saves.count(), 2);
        auto pending = qvariant_cast<QList<SavedEditorDocument>>(saves.at(1).at(0));
        QVERIFY(pending.at(0).sql.endsWith(" more"));
        recovery.saved(saves.at(1).at(1).toULongLong());
        QTRY_COMPARE(closed.count(), 1);
    }
    void failurePreservesSnapshotAndCloseWaitsForLatestAcknowledgement() {
        QTabWidget tabs;
        auto add = [&] {
            auto* e = new SqlEditor;
            tabs.addTab(e, "Untitled");
            return e;
        };
        add();
        WorkspaceRecoveryController recovery(&tabs, add);
        QSignalSpy restores(&recovery, &WorkspaceRecoveryController::restoreRequested);
        QSignalSpy saves(&recovery, &WorkspaceRecoveryController::saveRequested);
        QSignalSpy closed(&recovery, &WorkspaceRecoveryController::closeReady);
        recovery.start();
        auto first = restores.at(0).at(0).toULongLong();
        recovery.failed(first, "Storage unavailable");
        recovery.flush();
        QCOMPARE(saves.count(), 0);
        QVERIFY(!tabs.isEnabled());
        recovery.retry();
        QCOMPARE(restores.count(), 2);
        recovery.restored(first, {});
        QVERIFY(!tabs.isEnabled());
        recovery.restored(restores.at(1).at(0).toULongLong(), {});
        QVERIFY(tabs.isEnabled());
        qobject_cast<SqlEditor*>(tabs.widget(0))->setText("SELECT 1;");
        recovery.requestClose();
        QVERIFY(!tabs.isEnabled());
        QCOMPARE(closed.count(), 0);
        QCOMPARE(saves.count(), 1);
        recovery.failed(saves.at(0).at(1).toULongLong(), "Disk full");
        QCOMPARE(closed.count(), 0);
        recovery.retry();
        QCOMPARE(saves.count(), 2);
        recovery.saved(saves.at(0).at(1).toULongLong());
        QCOMPARE(closed.count(), 0);
        recovery.saved(saves.at(1).at(1).toULongLong());
        QTRY_COMPARE(closed.count(), 1);
    }
};
QTEST_MAIN(RecoveryTest)
#include "recovery_test.moc"
