#include "app/main_window.h"
#include "app/object_explorer.h"
#include "app/object_tab_title.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "design_system/icons.h"
#include "design_system/theme.h"
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
namespace {
SavedWorkspaceTab sqlTab(const SavedEditorDocument& document) {
    SavedWorkspaceTab tab;
    tab.document = document;
    return tab;
}
SavedWorkspaceTab sqlTab(const QString& id) {
    SavedEditorDocument document;
    document.id = id;
    document.title = "Untitled";
    return sqlTab(document);
}
QList<SavedEditorDocument> documentsOf(const QList<SavedWorkspaceTab>& tabs) {
    QList<SavedEditorDocument> documents;
    for (const auto& tab : tabs)
        if (!tab.isObject)
            documents.append(tab.document);
    return documents;
}
} // namespace
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
            QVERIFY(adapter.saveWorkspaceTabs({sqlTab(document)}, 0, 1));
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
        const auto documents = documentsOf(controller->snapshotTabs());
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
        const auto restoredDocuments = documentsOf(restoredController->snapshotTabs());
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
        object.objectId = "pg:relation:42";
        object.label = "\"public\".\"orders\"";
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
        QCOMPARE(tabs->tabText(0), QString("orders"));
        QVERIFY(!tabs->tabIcon(0).isNull());
        QCOMPARE(objectTabTitle("pg:relation:43", R"("odd.schema"."order.items")"),
                 QString("order.items"));
        QCOMPARE(objectTabTitle("legacy", "`db`.`order.part`"), QString("order.part"));
        QCOMPARE(objectTabTitle(R"(["db","order.part"])", "`db`.`order.part`"),
                 QString("order.part"));
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
        auto* reconnect = explorer->findChild<QAction*>("objectReconnect");
        QVERIFY(reconnect);
        reconnect->trigger();
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
        QCOMPARE(restoredTabs->tabText(0), QString("orders"));
        QVERIFY(!restoredTabs->tabIcon(0).isNull());
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

    void savedDataAndErdPanesRestoreInertWithStableValues() {
        for (const auto persistedPane : {quint32(4), quint32(5)}) {
            QTemporaryDir directory;
            const auto path = directory.filePath("pane.sqlite");
            SavedWorkspaceTab object;
            object.isObject = true;
            object.profileId = "profile:missing";
            object.objectType = "table";
            object.objectId = "main.orders";
            object.label = "orders";
            object.pane = persistedPane;
            {
                EngineAdapter adapter(nullptr, path);
                QSignalSpy saved(&adapter, &EngineAdapter::workspaceSaved);
                QVERIFY(adapter.saveWorkspaceTabs({object}, 0, 1));
                QTRY_COMPARE(saved.count(), 1);
            }
            MainWindow window(nullptr, path);
            auto* controller = window.findChild<WorkspaceRecoveryController*>();
            auto* tabs = window.findChild<QTabWidget*>("editorTabs");
            auto* workspace = window.findChild<QueryWorkspace*>();
            QVERIFY(controller);
            QVERIFY(tabs);
            QVERIFY(workspace);
            QSignalSpy reads(workspace->adapter(), &EngineAdapter::objectInspectionReady);
            QSignalSpy failures(workspace->adapter(), &EngineAdapter::objectInspectionFailed);
            controller->start();
            QTRY_VERIFY(controller->isReady());
            QCOMPARE(tabs->count(), 1);
            auto* explorer = qobject_cast<ObjectExplorer*>(tabs->widget(0));
            QVERIFY(explorer);
            QCOMPARE(explorer->paneIndex(), persistedPane == 4 ? 5 : 4);
            QVERIFY(explorer->needsConnection());
            QCOMPARE(controller->snapshotTabs().at(0).pane, persistedPane);
            QCOMPARE(reads.count(), 0);
            QCOMPARE(failures.count(), 0);
        }
    }
    void duplicateObjectTabsRestoreAsSeparateTabs() {
        // Rust persists the same object twice with independent panes; restoring that
        // snapshot must not be rejected by the native workspace.
        QTabWidget tabs;
        WorkspaceRecoveryController recovery(&tabs, [] { return new SqlEditor; });
        recovery.setObjectFactory([](const SavedWorkspaceTab&) -> QWidget* { return new QWidget; });
        QSignalSpy restores(&recovery, &WorkspaceRecoveryController::restoreTabsRequested);
        QSignalSpy errors(&recovery, &WorkspaceRecoveryController::errorOccurred);
        recovery.start();
        SavedWorkspaceTab first;
        first.isObject = true;
        first.profileId = "profile:local";
        first.objectType = "table";
        first.objectId = "main.orders";
        first.label = "orders";
        first.pane = 0;
        SavedWorkspaceTab second = first;
        second.pane = 5;
        recovery.restoredTabs(restores.at(0).at(0).toULongLong(), {first, second}, 1);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(tabs.count(), 2);
        QCOMPARE(tabs.currentIndex(), 1);
    }

    void restoredViewUsesEyeIconAndOtherKindsKeepTheirIcon() {
        QTabWidget tabs;
        WorkspaceRecoveryController recovery(&tabs, [] { return new SqlEditor; });
        recovery.setObjectFactory([](const SavedWorkspaceTab&) -> QWidget* { return new QWidget; });
        QSignalSpy restores(&recovery, &WorkspaceRecoveryController::restoreTabsRequested);
        recovery.start();
        QCOMPARE(restores.count(), 1);
        SavedWorkspaceTab view;
        view.isObject = true;
        view.profileId = "profile:missing";
        view.objectType = "view";
        view.objectId = "main.summary";
        view.label = "summary";
        SavedWorkspaceTab function = view;
        function.objectType = "function";
        function.objectId = "main.calculate";
        function.label = "calculate";
        recovery.restoredTabs(restores.at(0).at(0).toULongLong(), {view, function}, 0);
        QCOMPARE(tabs.count(), 2);
        const auto color = design::resolvedThemeForWidget(tabs).colors.mutedText;
        QCOMPARE(tabs.tabIcon(0).pixmap(16, 16).toImage(),
                 design::themedIcon(design::Icon::Eye, color, 16).pixmap(16, 16).toImage());
        QCOMPARE(tabs.tabIcon(1).pixmap(16, 16).toImage(),
                 design::themedIcon(design::Icon::File, color, 16).pixmap(16, 16).toImage());
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
        QSignalSpy restores(&recovery, &WorkspaceRecoveryController::restoreTabsRequested);
        QSignalSpy saves(&recovery, &WorkspaceRecoveryController::saveTabsRequested);
        QSignalSpy errors(&recovery, &WorkspaceRecoveryController::errorOccurred);
        recovery.start();
        recovery.restoredTabs(restores.at(0).at(0).toULongLong(), {sqlTab("pending")}, 0);
        auto* editor = qobject_cast<SqlEditor*>(tabs.widget(0));
        editor->openFile(file.fileName());
        QVERIFY(editor->isIoBusy());
        recovery.requestClose();
        QCoreApplication::processEvents();
        QCOMPARE(saves.count(), 0);
        QCOMPARE(errors.count(), 0);
        release.release();
        QTRY_COMPARE(saves.count(), 1);
        const auto documents =
            documentsOf(qvariant_cast<QList<SavedWorkspaceTab>>(saves.at(0).at(0)));
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
        QSignalSpy restores(&recovery, &WorkspaceRecoveryController::restoreTabsRequested);
        QSignalSpy saves(&recovery, &WorkspaceRecoveryController::saveTabsRequested);
        QSignalSpy errors(&recovery, &WorkspaceRecoveryController::errorOccurred);
        QSignalSpy closed(&recovery, &WorkspaceRecoveryController::closeReady);
        recovery.start();
        recovery.requestClose();
        QCOMPARE(errors.count(), 0);
        QCOMPARE(closed.count(), 0);
        recovery.restoredTabs(restores.at(0).at(0).toULongLong(), {sqlTab("startup")}, 0);
        QVERIFY(!tabs.isEnabled());
        QCOMPARE(saves.count(), 1);
        recovery.saved(saves.at(0).at(2).toULongLong());
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
        QSignalSpy restores(&recovery, &WorkspaceRecoveryController::restoreTabsRequested);
        QSignalSpy saves(&recovery, &WorkspaceRecoveryController::saveTabsRequested);
        QSignalSpy closed(&recovery, &WorkspaceRecoveryController::closeReady);
        QSignalSpy errors(&recovery, &WorkspaceRecoveryController::errorOccurred);
        recovery.start();
        // Rust rejects an invalid snapshot and reports the restore as failed.
        recovery.failed(restores.at(0).at(0).toULongLong(), "Saved SQL tab is invalid.");
        QCOMPARE(errors.count(), 1);
        QCOMPARE(tabs.widget(0), original);
        QCOMPARE(original->text(), QString("original"));
        recovery.retry();
        recovery.restoredTabs(restores.at(1).at(0).toULongLong(), {sqlTab("valid")}, 0);
        recovery.requestClose();
        QCOMPARE(saves.count(), 1);
        recovery.saved(saves.at(0).at(2).toULongLong());
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
        QSignalSpy restores(&recovery, &WorkspaceRecoveryController::restoreTabsRequested);
        QSignalSpy saves(&recovery, &WorkspaceRecoveryController::saveTabsRequested);
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
        recovery.restoredTabs(token, {sqlTab(doc), sqlTab(second)}, 0);
        QVERIFY(tabs.isEnabled());
        auto* e = qobject_cast<SqlEditor*>(tabs.widget(0));
        QCOMPARE(e->text(), doc.sql);
        QCOMPARE(e->filePath(), doc.filePath);
        QVERIFY(e->isModified());
        const auto snapshot = documentsOf(recovery.snapshotTabs());
        QCOMPARE(snapshot.size(), 2);
        QCOMPARE(snapshot.at(0).id, doc.id);
        QCOMPARE(snapshot.at(1).id, second.id);
        QCOMPARE(snapshot.at(0).selectionAnchor, quint64(8));
        e->append(" -- edit");
        recovery.flush();
        QCOMPARE(saves.count(), 1);
        e->append(" more");
        recovery.flush();
        QCOMPARE(saves.count(), 1);
        recovery.requestClose();
        QCOMPARE(closed.count(), 0);
        recovery.saved(saves.at(0).at(2).toULongLong());
        QTRY_COMPARE(saves.count(), 2);
        auto pending = documentsOf(qvariant_cast<QList<SavedWorkspaceTab>>(saves.at(1).at(0)));
        QVERIFY(pending.at(0).sql.endsWith(" more"));
        recovery.saved(saves.at(1).at(2).toULongLong());
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
        QSignalSpy restores(&recovery, &WorkspaceRecoveryController::restoreTabsRequested);
        QSignalSpy saves(&recovery, &WorkspaceRecoveryController::saveTabsRequested);
        QSignalSpy closed(&recovery, &WorkspaceRecoveryController::closeReady);
        recovery.start();
        auto first = restores.at(0).at(0).toULongLong();
        recovery.failed(first, "Storage unavailable");
        recovery.flush();
        QCOMPARE(saves.count(), 0);
        QVERIFY(!tabs.isEnabled());
        recovery.retry();
        QCOMPARE(restores.count(), 2);
        recovery.restoredTabs(first, {sqlTab("stale")}, 0);
        QVERIFY(!tabs.isEnabled());
        recovery.restoredTabs(restores.at(1).at(0).toULongLong(), {sqlTab("current")}, 0);
        QVERIFY(tabs.isEnabled());
        qobject_cast<SqlEditor*>(tabs.widget(0))->setText("SELECT 1;");
        recovery.requestClose();
        QVERIFY(!tabs.isEnabled());
        QCOMPARE(closed.count(), 0);
        QCOMPARE(saves.count(), 1);
        recovery.failed(saves.at(0).at(2).toULongLong(), "Disk full");
        QCOMPARE(closed.count(), 0);
        recovery.retry();
        QCOMPARE(saves.count(), 2);
        recovery.saved(saves.at(0).at(2).toULongLong());
        QCOMPARE(closed.count(), 0);
        recovery.saved(saves.at(1).at(2).toULongLong());
        QTRY_COMPARE(closed.count(), 1);
    }
};
QTEST_MAIN(RecoveryTest)
#include "recovery_test.moc"
