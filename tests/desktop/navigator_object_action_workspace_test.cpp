#include "app/main_window.h"
#include "app/navigator_controller.h"
#include "app/object_explorer.h"
#include "app/object_data_workspace.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "navigator_sql_workspace_test.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

void NavigatorSqlWorkspaceTest::objectActionsRenameAndDropThroughNavigator() {
    using namespace choscordb;
    QTemporaryDir storage;
    MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    auto* workspace = window.findChild<QueryWorkspace*>();
    QSignalSpy connected(workspace, &QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(connected.count(), 1);
    const auto connection = connected.first().at(0).toULongLong();
    auto* adapter = workspace->adapter();
    int finished = 0, failed = 0;
    connect(adapter, &EngineAdapter::eventReady, &window, [&](const BridgeEvent& event) {
        if (event.kind == "query_finished")
            ++finished;
        if (event.kind == "query_failed")
            ++failed;
    });
    const auto create = adapter->execute(connection, "CREATE TABLE \"old.name\" (id INTEGER)");
    QVERIFY(create);
    adapter->fetchPage(*create);
    QTRY_COMPARE(finished, 1);
    adapter->releaseQuery(*create);
    const auto collision = adapter->execute(connection, "CREATE TABLE taken (id INTEGER)");
    QVERIFY(collision);
    adapter->fetchPage(*collision);
    QTRY_COMPARE(finished, 2);
    adapter->releaseQuery(*collision);

    auto* navigator = window.findChild<NavigatorController*>();
    auto* model = navigator->model();
    const auto root = model->index(0, 0);
    model->fetchMore(root);
    QTRY_VERIFY(root.data(NavigatorModel::ChildrenLoadedRole).toBool());
    const auto schema = model->index(0, 0, root);
    model->fetchMore(schema);
    QTRY_VERIFY(schema.data(NavigatorModel::ChildrenLoadedRole).toBool());
    const auto group = model->index(0, 0, schema);
    model->fetchMore(group);
    QTRY_VERIFY(group.data(NavigatorModel::ChildrenLoadedRole).toBool());
    const auto table = model->index(0, 0, group);
    QCOMPARE(table.data().toString(), QString("old.name"));
    const auto oldId = table.data(NavigatorModel::ObjectIdRole).toString();
    const auto oldLabel = table.data(NavigatorModel::QualifiedNameRole).toString();
    emit window.objectContextSelected(connection, oldId, oldLabel, "table");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* object = qobject_cast<ObjectExplorer*>(tabs->currentWidget());
    QVERIFY(object);
    QTRY_VERIFY(!object->operationInFlight());
    auto* data = object->findChild<ObjectDataWorkspace*>();
    QVERIFY(data);
    emit data->foreignKeyRequested(connection, oldId, oldLabel, QStringLiteral("id=1"));
    auto* secondObject = qobject_cast<ObjectExplorer*>(tabs->currentWidget());
    QVERIFY(secondObject && secondObject != object);
    QTRY_VERIFY(workspace->navigationAllowed());
    QTRY_VERIFY(!secondObject->operationInFlight());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* draft = qobject_cast<SqlEditor*>(tabs->currentWidget());
    QVERIFY(draft);
    draft->setText("-- unsaved query draft");
    QCOMPARE(tabs->count(), 3);
    auto* recovery = window.findChild<WorkspaceRecoveryController*>();
    QVERIFY(recovery);
    // Opening object tabs may finish background data requests before the actions below.
    const int settledFinished = finished;

    QMenu renameMenu;
    navigator->populateContextMenu(&renameMenu, table);
    auto* rename = renameMenu.findChild<QAction*>("renameObject");
    QVERIFY(rename && rename->isEnabled());
    workspace->setExternalWork(true);
    rename->trigger();
    QCOMPARE(finished, settledFinished);
    QVERIFY(!window.findChild<QDialog*>("renameObjectDialog"));
    workspace->setExternalWork(false);
    bool renameDialogChecked = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = window.findChild<QDialog*>("renameObjectDialog");
        if (!dialog) {
            if (auto* modal = QApplication::activeModalWidget())
                modal->close();
            return;
        }
        auto* name = dialog->findChild<QLineEdit*>("renameObjectName");
        auto* preview = dialog->findChild<QPlainTextEdit*>("renameObjectSql");
        auto* confirm = dialog->findChild<QPushButton*>("renameObjectConfirm");
        if (name && preview && confirm) {
            const auto initial = name->text() == QStringLiteral("old.name") &&
                                 !confirm->isEnabled();
            name->setText("new.name");
            renameDialogChecked =
                initial && preview->toPlainText() ==
                               QStringLiteral("ALTER TABLE \"main\".\"old.name\" RENAME TO "
                                              "\"new.name\";") &&
                confirm->isEnabled();
            if (renameDialogChecked) {
                confirm->click();
                return;
            }
        }
        dialog->reject();
    });
    rename->trigger();
    QVERIFY(renameDialogChecked);
    QTRY_VERIFY(finished > settledFinished);
    QTRY_VERIFY(group.data(NavigatorModel::ChildrenLoadedRole).toBool());
    QTRY_COMPARE(model->index(0, 0, group).data().toString(), QString("new.name"));
    QCOMPARE(tabs->count(), 3);
    QCOMPARE(tabs->tabText(tabs->indexOf(object)), QString("new.name"));
    QCOMPARE(tabs->tabText(tabs->indexOf(secondObject)), QString("new.name"));
    QCOMPARE(object->property("objectId").toString(),
             model->index(0, 0, group).data(NavigatorModel::ObjectIdRole).toString());
    QCOMPARE(secondObject->property("objectId").toString(), object->property("objectId").toString());
    int recoveredObjects = 0;
    for (const auto& saved : recovery->snapshotTabs())
        if (saved.isObject && saved.objectId == object->property("objectId").toString())
            ++recoveredObjects;
    QCOMPARE(recoveredObjects, 2);
    QTRY_VERIFY(!object->operationInFlight());
    QTRY_VERIFY(!secondObject->operationInFlight());
    QTRY_VERIFY(workspace->navigationAllowed());

    QMenu collisionMenu;
    navigator->populateContextMenu(&collisionMenu, model->index(0, 0, group));
    auto* collisionRename = collisionMenu.findChild<QAction*>("renameObject");
    QVERIFY(collisionRename);
    bool collisionSubmitted = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = window.findChild<QDialog*>("renameObjectDialog");
        auto* name = dialog ? dialog->findChild<QLineEdit*>("renameObjectName") : nullptr;
        auto* confirm = dialog ? dialog->findChild<QPushButton*>("renameObjectConfirm") : nullptr;
        if (name && confirm) {
            name->setText("taken");
            collisionSubmitted = confirm->isEnabled();
            if (collisionSubmitted) {
                confirm->click();
                return;
            }
        }
        if (auto* modal = QApplication::activeModalWidget())
            modal->close();
    });
    collisionRename->trigger();
    QVERIFY(collisionSubmitted);
    QTRY_COMPARE(failed, 1);
    QCOMPARE(tabs->count(), 3);
    QCOMPARE(object->property("objectId").toString(),
             model->index(0, 0, group).data(NavigatorModel::ObjectIdRole).toString());
    QTRY_COMPARE(window.findChild<ToastRegion*>("toastRegion")->property("variant").toString(),
                 QString("danger"));

    const auto renamed = model->index(0, 0, group);
    QMenu dropMenu;
    navigator->populateContextMenu(&dropMenu, renamed);
    auto* drop = dropMenu.findChild<QAction*>("dropObject");
    QVERIFY(drop && drop->isEnabled());
    const int beforeCancelledDrop = finished;
    bool dropDialogChecked = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = window.findChild<ConfirmationDialog*>("dropObjectDialog");
        if (!dialog) {
            if (auto* modal = QApplication::activeModalWidget())
                modal->close();
            return;
        }
        dropDialogChecked = dialog->text().contains("DROP TABLE \"main\".\"new.name\"") &&
                            dialog->defaultButton() &&
                            dialog->defaultButton()->text() == QStringLiteral("Cancel");
        dialog->reject();
    });
    drop->trigger();
    QVERIFY(dropDialogChecked);
    QCOMPARE(finished, beforeCancelledDrop);
    QCOMPARE(tabs->count(), 3);

    bool dropConfirmed = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = window.findChild<ConfirmationDialog*>("dropObjectDialog");
        auto* confirm = dialog ? dialog->findChild<QPushButton*>("dropObjectConfirm") : nullptr;
        if (confirm) {
            dropConfirmed = true;
            confirm->click();
        } else if (auto* modal = QApplication::activeModalWidget()) {
            modal->close();
        }
    });
    drop->trigger();
    QVERIFY(dropConfirmed);
    QTRY_VERIFY(finished > beforeCancelledDrop);
    QTRY_VERIFY(group.data(NavigatorModel::ChildrenLoadedRole).toBool());
    QTRY_COMPARE(model->rowCount(group), 1);
    QCOMPARE(model->index(0, 0, group).data().toString(), QString("taken"));
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(tabs->currentWidget(), draft);
    QCOMPARE(draft->text(), QString("-- unsaved query draft"));
    QCOMPARE(recovery->snapshotTabs().size(), 1);

    const auto createView =
        adapter->execute(connection, "CREATE VIEW \"v\" AS SELECT id FROM taken");
    QVERIFY(createView);
    const int beforeCreateView = finished;
    adapter->fetchPage(*createView);
    QTRY_VERIFY(finished > beforeCreateView);
    adapter->releaseQuery(*createView);
    const auto viewGroup = model->index(1, 0, schema);
    QCOMPARE(viewGroup.data().toString(), QString("Views"));
    model->fetchMore(viewGroup);
    QTRY_VERIFY(viewGroup.data(NavigatorModel::ChildrenLoadedRole).toBool());
    const auto view = model->index(0, 0, viewGroup);
    QCOMPARE(view.data().toString(), QString("v"));
    QMenu viewMenu;
    navigator->populateContextMenu(&viewMenu, view);
    auto* dropView = viewMenu.findChild<QAction*>("dropObject");
    auto* renameView = viewMenu.findChild<QAction*>("renameObject");
    QVERIFY(dropView && dropView->isEnabled());
    QVERIFY(renameView && !renameView->isEnabled());
    const int beforeDropView = finished;
    bool viewSubmitted = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = window.findChild<ConfirmationDialog*>("dropObjectDialog");
        auto* confirm = dialog ? dialog->findChild<QPushButton*>("dropObjectConfirm") : nullptr;
        if (confirm) {
            viewSubmitted = dialog->text().contains(QStringLiteral("DROP VIEW \"main\".\"v\";"));
            confirm->click();
        } else if (auto* modal = QApplication::activeModalWidget()) {
            modal->close();
        }
    });
    dropView->trigger();
    QVERIFY(viewSubmitted);
    QTRY_VERIFY(finished > beforeDropView);
    QTRY_VERIFY(viewGroup.data(NavigatorModel::ChildrenLoadedRole).toBool());
    QTRY_COMPARE(model->rowCount(viewGroup), 0);
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(draft->text(), QString("-- unsaved query draft"));
}

void NavigatorSqlWorkspaceTest::objectActionKeepsOtherSessionTabWithSameProfile() {
    using namespace choscordb;
    QTemporaryDir storage;
    MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    auto* workspace = window.findChild<QueryWorkspace*>();
    SavedProfile profile;
    profile.id = QStringLiteral("shared-profile");
    profile.name = QStringLiteral("Shared database");
    profile.path = storage.filePath("shared.sqlite");
    QSignalSpy connected(workspace, &QueryWorkspace::connectionReady);
    const auto first = workspace->connectSavedProfile(profile);
    const auto second = workspace->connectSavedProfile(profile);
    QVERIFY(first && second && *first != *second);
    QTRY_COMPARE(connected.count(), 2);
    auto* adapter = workspace->adapter();
    int finished = 0;
    connect(adapter, &EngineAdapter::eventReady, &window, [&](const BridgeEvent& event) {
        if (event.kind == "query_finished")
            ++finished;
    });
    const auto create = adapter->execute(*first, "CREATE TABLE shared_table (id INTEGER)");
    QVERIFY(create);
    adapter->fetchPage(*create);
    QTRY_COMPARE(finished, 1);
    adapter->releaseQuery(*create);
    auto* navigator = window.findChild<NavigatorController*>();
    auto* model = navigator->model();
    QModelIndex root;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto candidate = model->index(row, 0);
        if (candidate.data(NavigatorModel::ConnectionRole).toULongLong() == *first) {
            root = candidate;
            break;
        }
    }
    QVERIFY(root.isValid());
    model->fetchMore(root);
    QTRY_VERIFY(root.data(NavigatorModel::ChildrenLoadedRole).toBool());
    const auto schema = model->index(0, 0, root);
    model->fetchMore(schema);
    QTRY_VERIFY(schema.data(NavigatorModel::ChildrenLoadedRole).toBool());
    const auto group = model->index(0, 0, schema);
    model->fetchMore(group);
    QTRY_VERIFY(group.data(NavigatorModel::ChildrenLoadedRole).toBool());
    const auto table = model->index(0, 0, group);
    const auto objectId = table.data(NavigatorModel::ObjectIdRole).toString();
    const auto label = table.data(NavigatorModel::QualifiedNameRole).toString();
    emit window.objectContextSelected(*first, objectId, label, "table");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* firstObject = qobject_cast<ObjectExplorer*>(tabs->currentWidget());
    QVERIFY(firstObject);
    QTRY_VERIFY(!firstObject->operationInFlight());
    auto* data = firstObject->findChild<ObjectDataWorkspace*>();
    QVERIFY(data);
    emit data->foreignKeyRequested(*second, objectId, label, QStringLiteral("id=1"));
    auto* secondObject = qobject_cast<ObjectExplorer*>(tabs->currentWidget());
    QVERIFY(secondObject && secondObject != firstObject);
    QTRY_VERIFY(workspace->navigationAllowed());
    QCOMPARE(secondObject->property("objectProfileId").toString(),
             firstObject->property("objectProfileId").toString());
    QCOMPARE(secondObject->property("objectConnection").toULongLong(), *second);

    QMenu menu;
    navigator->populateContextMenu(&menu, table);
    auto* drop = menu.findChild<QAction*>("dropObject");
    QVERIFY(drop);
    const int beforeDrop = finished;
    bool submitted = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = window.findChild<ConfirmationDialog*>("dropObjectDialog");
        auto* confirm = dialog ? dialog->findChild<QPushButton*>("dropObjectConfirm") : nullptr;
        if (confirm) {
            submitted = true;
            confirm->click();
        } else if (auto* modal = QApplication::activeModalWidget()) {
            modal->close();
        }
    });
    drop->trigger();
    QVERIFY(submitted);
    QTRY_VERIFY(finished > beforeDrop);
    QTRY_COMPARE(tabs->count(), 1);
    QCOMPARE(tabs->currentWidget(), secondObject);
    QCOMPARE(secondObject->property("objectId").toString(), objectId);
    QCOMPARE(secondObject->property("objectConnection").toULongLong(), *second);
    QCOMPARE(window.findChild<WorkspaceRecoveryController*>()->snapshotTabs().first().objectId,
             objectId);
}
