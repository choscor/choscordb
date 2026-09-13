#include "app/main_window.h"
#include "app/navigator_controller.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "models/navigator_model.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QtTest>
class NavigatorSqlWorkspaceTest : public QObject {
    Q_OBJECT
  private slots:
    void disconnectMenuKeepsOtherSessionsAndDrafts() {
        choscordb::MainWindow window;
        window.show();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        workspace->connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 1);
        workspace->connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 2);
        const auto second = connected.at(1).at(0).toULongLong();
        auto* navigator = window.findChild<choscordb::NavigatorController*>();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        editor->setText("-- keep this draft");
        QMenu menu;
        navigator->populateContextMenu(&menu, navigator->model()->index(0, 0));
        auto* action = menu.findChild<QAction*>("disconnectSession");
        QVERIFY(action);
        bool observed = false;
        QTimer::singleShot(0, &window, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box)
                return;
            observed = true;
            QVERIFY(box->grab().save("native-disconnect.png"));
            QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel));
            box->button(QMessageBox::Cancel)->click();
        });
        action->trigger();
        QVERIFY(observed);
        QCOMPARE(navigator->model()->rowCount(), 2);
        QTimer::singleShot(0, &window, [] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            for (auto* button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
                    button->click();
                    return;
                }
            box->reject();
        });
        action->trigger();
        QTRY_COMPARE(navigator->model()->rowCount(), 1);
        QCOMPARE(window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong(),
                 second);
        QCOMPARE(editor->text(), QString("-- keep this draft"));
    }
    void generationOpensDraftOnExistingSavedConnectionWithoutExecuting() {
        choscordb::MainWindow window;
        window.show();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        for (auto* action : window.findChildren<QAction*>())
            if (action->text() == "New SQLite session…")
                action->trigger();
        auto* profiles = window.findChild<choscordb::ProfileDialog*>();
        QVERIFY(profiles);
        auto* save = profiles->findChild<QPushButton*>("profileSave");
        QTRY_VERIFY(save->isEnabled());
        choscordb::SavedProfile profile;
        profile.id = "generated-sql-profile";
        profile.name = "Generated SQL fixture";
        profile.path = ":memory:";
        profiles->saveDraft(profile);
        QTRY_COMPARE(profiles->findChild<QListWidget*>("profileList")->count(), 1);
        QTRY_VERIFY(save->isEnabled());
        profiles->selectProfile(profile.id);
        profiles->findChild<QPushButton*>("profileConnect")->click();
        QTRY_COMPARE(connected.count(), 1);
        profiles->hide();
        const auto id = connected.first().at(0).toULongLong();
        int started = 0, finished = 0;
        auto* adapter = workspace->adapter();
        connect(
            adapter, &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& event) {
                const auto kind =
                    QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
                if (kind == "query_state" &&
                    QString::fromUtf8(event.state.data(), qsizetype(event.state.size())) ==
                        "queued")
                    ++started;
                if (kind == "query_finished")
                    ++finished;
            },
            Qt::DirectConnection);
        auto query = adapter->execute(id, "CREATE TABLE \"a.b\" (\"col name\" TEXT)");
        QVERIFY(query);
        adapter->fetchPage(*query);
        QTRY_COMPARE(finished, 1);
        QCOMPARE(started, 1);
        adapter->releaseQuery(*query);
        auto* navigator = window.findChild<choscordb::NavigatorController*>();
        auto* model = navigator->model();
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_VERIFY(root.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
        const auto schema = model->index(0, 0, root);
        model->fetchMore(schema);
        QTRY_VERIFY(schema.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
        const auto table = model->index(0, 0, schema);
        QCOMPARE(table.data().toString(), QString("a.b"));
        model->fetchMore(table);
        QTRY_VERIFY(table.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
        workspace->connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 2);
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* original = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        original->setText("-- preserved draft");
        const int tabCount = tabs->count(), queryCount = started;
        QMenu menu;
        navigator->populateContextMenu(&menu, table);
        auto* select = menu.findChild<QAction*>("generate_select");
        QVERIFY(select);
        select->trigger();
        QCOMPARE(tabs->count(), tabCount + 1);
        auto* generated = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QCOMPARE(generated->text(), QString("SELECT * FROM \"main\".\"a.b\";"));
        QVERIFY(generated->isModified());
        QCOMPARE(generated->property("profileId").toString(), profile.id);
        QCOMPARE(window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong(),
                 id);
        QCOMPARE(original->text(), QString("-- preserved draft"));
        menu.findChild<QAction*>("generate_insert")->trigger();
        QCOMPARE(tabs->count(), tabCount + 2);
        auto* insert = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QVERIFY(
            insert->text().contains("INSERT INTO \"main\".\"a.b\" (\"col name\") VALUES ($1);"));
        QVERIFY(insert->text().startsWith("--"));
        menu.findChild<QAction*>("generate_update")->trigger();
        QCOMPARE(tabs->count(), tabCount + 3);
        auto* update = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QVERIFY(update->text().contains(
            "UPDATE \"main\".\"a.b\" SET \"col name\" = $1 WHERE /* predicate */;"));
        menu.findChild<QAction*>("generate_delete")->trigger();
        QCOMPARE(tabs->count(), tabCount + 4);
        auto* remove = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QCOMPARE(remove->text(), QString("DELETE FROM \"main\".\"a.b\" WHERE /* predicate */;"));
        tabs->setCurrentWidget(insert);
        QCOMPARE(started, queryCount);
        QCOMPARE(connected.count(), 2);
        QVERIFY(window.grab().save("native-navigator-sql.png"));
        QSignalSpy shutdown(adapter, &choscordb::EngineAdapter::shutdownReady);
        adapter->beginShutdown();
        QTRY_COMPARE(shutdown.count(), 1);
        // Actor disconnects and the metadata barrier drain accepted work, proving
        // no generated draft submitted another queued query even asynchronously.
        QCOMPARE(started, queryCount);
    }
};
QTEST_MAIN(NavigatorSqlWorkspaceTest)
#include "navigator_sql_workspace_test.moc"
