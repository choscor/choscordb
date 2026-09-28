#include "app/main_window.h"
#include "app/navigator_controller.h"
#include "app/pin_store.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "models/navigator_model.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>

class PinnedMenuProbe : public QObject {
  public:
    bool sawUnpin = false;

  protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() != QEvent::Show || object->objectName() != QStringLiteral("pinnedMenu"))
            return false;
        auto* menu = qobject_cast<QMenu*>(object);
        auto* action = menu ? menu->findChild<QAction*>("unpinPinnedObject") : nullptr;
        if (action) {
            sawUnpin = true;
            action->trigger();
        }
        return false;
    }
};

class PinningFlowTest : public QObject {
    Q_OBJECT
  private slots:
    void rejectedConnectionCanBeRetriedFromTheSamePin() {
        using namespace choscordb;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto storePath = directory.filePath("profiles.sqlite");
        PinRecord pin;
        pin.profileId = "retry-profile";
        pin.profileName = "Retry";
        pin.objectId = "missing-table";
        pin.name = "orders";
        pin.qualifiedName = "main.orders";
        pin.kind = "table";
        pin.parentObjectId = "tables-group";
        pin.ancestryIds = {"main", "tables-group"};
        pin.ancestryNames = {"main", "Tables"};
        PinStore store(storePath);
        QString error;
        QVERIFY2(store.save({pin}, &error), qPrintable(error));
        MainWindow window(nullptr, storePath);
        window.show();
        auto* workspace = window.findChild<QueryWorkspace*>();
        auto* profiles = window.findChild<QListWidget*>("savedConnections");
        auto* pins = window.findChild<QListWidget*>("pinnedList");
        QVERIFY(workspace && profiles && pins);
        SavedProfile profile;
        profile.id = pin.profileId;
        profile.name = pin.profileName;
        profile.path = directory.filePath("database.sqlite");
        workspace->adapter()->saveProfile(profile, 6201);
        QTRY_COMPARE(profiles->count(), 1);
        QTRY_COMPARE(pins->count(), 1);
        profiles->setCurrentItem(nullptr);
        bool guardRaised = false;
        connect(profiles, &QListWidget::currentItemChanged, &window,
                [&](QListWidgetItem* current, QListWidgetItem*) {
                    if (current && !guardRaised) {
                        guardRaised = true;
                        workspace->setExternalWork(true);
                    }
                });
        QSignalSpy opened(workspace, &QueryWorkspace::connectionReady);
        QTest::mouseClick(pins->viewport(), Qt::LeftButton, Qt::NoModifier,
                          pins->visualItemRect(pins->item(0)).center());
        QVERIFY(guardRaised);
        QTRY_VERIFY(window.findChild<QMessageBox*>("sidebarConnectionFailure"));
        QCOMPARE(opened.count(), 0);
        QCOMPARE(pins->count(), 1);
        workspace->setExternalWork(false);
        window.findChild<QMessageBox*>("sidebarConnectionFailure")->accept();
        QTest::mouseClick(pins->viewport(), Qt::LeftButton, Qt::NoModifier,
                          pins->visualItemRect(pins->item(0)).center());
        QTRY_COMPARE(opened.count(), 1);
    }

    void confirmedRenameAndDropUpdateOnlyTheShortcut() {
        using namespace choscordb;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MainWindow window(nullptr, directory.filePath("profiles.sqlite"));
        window.show();
        auto* workspace = window.findChild<QueryWorkspace*>();
        auto* profiles = window.findChild<QListWidget*>("savedConnections");
        auto* pins = window.findChild<QListWidget*>("pinnedList");
        auto* navigator = window.findChild<NavigatorController*>();
        QVERIFY(workspace && profiles && pins && navigator);
        SavedProfile profile;
        profile.id = "actions-profile";
        profile.name = "Actions";
        profile.path = directory.filePath("database.sqlite");
        workspace->adapter()->saveProfile(profile, 6101);
        QTRY_COMPARE(profiles->count(), 1);
        QSignalSpy connected(workspace, &QueryWorkspace::connectionReady);
        QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                          profiles->visualItemRect(profiles->item(0)).center());
        QTRY_COMPARE(connected.count(), 1);
        const auto connection = connected.first().at(0).toULongLong();
        auto* adapter = workspace->adapter();
        int completed = 0;
        connect(adapter, &EngineAdapter::eventReady, &window, [&](const BridgeEvent& event) {
            if (event.kind == "query_finished")
                ++completed;
        });
        const auto create = adapter->execute(connection, "CREATE TABLE orders (id INTEGER)");
        QVERIFY(create);
        adapter->fetchPage(*create);
        QTRY_COMPARE(completed, 1);
        adapter->releaseQuery(*create);
        auto* model = navigator->model();
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_VERIFY(root.data(NavigatorModel::ChildrenLoadedRole).toBool());
        const auto database = model->index(0, 0, root);
        model->fetchMore(database);
        QTRY_VERIFY(database.data(NavigatorModel::ChildrenLoadedRole).toBool());
        const auto group = model->index(0, 0, database);
        model->fetchMore(group);
        QTRY_VERIFY(group.data(NavigatorModel::ChildrenLoadedRole).toBool());
        const auto table = model->index(0, 0, group);
        QMenu pinMenu;
        navigator->populateContextMenu(&pinMenu, table);
        auto* pin = pinMenu.findChild<QAction*>("pinObject");
        QVERIFY(pin && pin->isEnabled());
        pin->trigger();
        QTRY_COMPARE(pins->count(), 1);

        QMenu renameMenu;
        navigator->populateContextMenu(&renameMenu, table);
        auto* rename = renameMenu.findChild<QAction*>("renameObject");
        QVERIFY(rename && rename->isEnabled());
        bool renameDialogSeen = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("renameObjectDialog");
            auto* name = dialog ? dialog->findChild<QLineEdit*>("renameObjectName") : nullptr;
            auto* confirm =
                dialog ? dialog->findChild<QPushButton*>("renameObjectConfirm") : nullptr;
            if (!name || !confirm) {
                if (auto* modal = QApplication::activeModalWidget())
                    modal->close();
                return;
            }
            renameDialogSeen = true;
            name->setText("orders_new");
            confirm->click();
        });
        rename->trigger();
        QVERIFY(renameDialogSeen);
        QTRY_COMPARE(completed, 2);
        QTRY_VERIFY(pins->item(0)->text().contains("orders_new"));
        QTRY_VERIFY(group.data(NavigatorModel::ChildrenLoadedRole).toBool());
        QCOMPARE(model->index(0, 0, group).data().toString(), QString("orders_new"));

        QMenu dropMenu;
        navigator->populateContextMenu(&dropMenu, model->index(0, 0, group));
        auto* drop = dropMenu.findChild<QAction*>("dropObject");
        QVERIFY(drop && drop->isEnabled());
        bool dropDialogSeen = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QMessageBox*>("dropObjectDialog");
            auto* confirm = dialog ? dialog->findChild<QPushButton*>("dropObjectConfirm") : nullptr;
            if (!confirm) {
                if (auto* modal = QApplication::activeModalWidget())
                    modal->close();
                return;
            }
            dropDialogSeen = true;
            confirm->click();
        });
        drop->trigger();
        QVERIFY(dropDialogSeen);
        QTRY_COMPARE(completed, 3);
        QTRY_VERIFY(pins->item(0)->toolTip().contains("Unavailable"));
        QCOMPARE(pins->count(), 1);
    }

    void savedObjectPinPersistsWithoutConnectingAtStartup() {
        using namespace choscordb;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto store = directory.filePath("profiles.sqlite");
        {
            MainWindow window(nullptr, store);
            window.show();
            auto* workspace = window.findChild<QueryWorkspace*>();
            auto* profiles = window.findChild<QListWidget*>("savedConnections");
            auto* pins = window.findChild<QListWidget*>("pinnedList");
            auto* pinnedSection = window.findChild<QWidget*>("pinnedSection");
            auto* navigator = window.findChild<NavigatorController*>();
            QVERIFY(workspace && profiles && pins && pinnedSection && navigator);
            SavedProfile profile;
            profile.id = "profile-alpha";
            profile.name = "Alpha";
            profile.path = directory.filePath("database.sqlite");
            workspace->adapter()->saveProfile(profile, 6001);
            QTRY_COMPARE(profiles->count(), 1);
            QSignalSpy connected(workspace, &QueryWorkspace::connectionReady);
            QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                              profiles->visualItemRect(profiles->item(0)).center());
            QTRY_COMPARE(connected.count(), 1);
            const auto connection = connected.first().at(0).toULongLong();
            auto* adapter = workspace->adapter();
            int finished = 0;
            connect(adapter, &EngineAdapter::eventReady, &window, [&](const BridgeEvent& event) {
                if (event.kind == "query_finished")
                    ++finished;
            });
            const auto query = adapter->execute(connection, "CREATE TABLE orders (id INTEGER)");
            QVERIFY(query);
            adapter->fetchPage(*query);
            QTRY_COMPARE(finished, 1);
            adapter->releaseQuery(*query);
            auto* model = navigator->model();
            const auto root = model->index(0, 0);
            model->fetchMore(root);
            QTRY_VERIFY(root.data(NavigatorModel::ChildrenLoadedRole).toBool());
            const auto database = model->index(0, 0, root);
            model->fetchMore(database);
            QTRY_VERIFY(database.data(NavigatorModel::ChildrenLoadedRole).toBool());
            const auto group = model->index(0, 0, database);
            model->fetchMore(group);
            QTRY_VERIFY(group.data(NavigatorModel::ChildrenLoadedRole).toBool());
            const auto table = model->index(0, 0, group);
            QCOMPARE(table.data(NavigatorModel::KindRole).toString(), QString("table"));
            QMenu menu;
            navigator->populateContextMenu(&menu, table);
            auto* action = menu.findChild<QAction*>("pinObject");
            QVERIFY(action && action->isEnabled());
            action->trigger();
            QTRY_COMPARE(pins->count(), 1);
            QVERIFY(pins->item(0)->text().contains("orders"));
            QMenu after;
            navigator->populateContextMenu(&after, table);
            QVERIFY(after.findChild<QAction*>("unpinObject"));
            const auto pinPoint = pins->visualItemRect(pins->item(0)).center();
            QCOMPARE(pins->itemAt(pinPoint), pins->item(0));
            PinnedMenuProbe probe;
            qApp->installEventFilter(&probe);
            QVERIFY(QMetaObject::invokeMethod(pins, "customContextMenuRequested",
                                              Q_ARG(QPoint, pinPoint)));
            qApp->removeEventFilter(&probe);
            QVERIFY(probe.sawUnpin);
            QTRY_COMPARE(pins->count(), 0);
            QVERIFY(!pinnedSection->isVisible());
            QCOMPARE(table.data(Qt::DisplayRole).toString(), QString("orders"));
            QMenu again;
            navigator->populateContextMenu(&again, table);
            auto* repin = again.findChild<QAction*>("pinObject");
            QVERIFY(repin && repin->isEnabled());
            repin->trigger();
            QTRY_COMPARE(pins->count(), 1);
            QVERIFY(pinnedSection->isVisible());
        }
        MainWindow restored(nullptr, store);
        restored.show();
        auto* pins = restored.findChild<QListWidget*>("pinnedList");
        auto* tree = restored.findChild<QTreeView*>("databaseNavigator");
        auto* filter = restored.findChild<QLineEdit*>("navigatorFilter");
        auto* profiles = restored.findChild<QListWidget*>("savedConnections");
        auto* selector = restored.findChild<QComboBox*>("connectionSelector");
        auto* workspace = restored.findChild<QueryWorkspace*>();
        QVERIFY(pins && tree && filter && profiles && selector && workspace);
        QTRY_COMPARE(pins->count(), 1);
        QCOMPARE(tree->model()->rowCount(), 0);
        QVERIFY(pins->item(0)->toolTip().contains("Alpha"));
        QTRY_COMPARE(profiles->count(), 1);
        const auto editorTarget = selector->currentData();
        filter->setText("nothing_matches");
        QCOMPARE(pins->count(), 1);
        QSignalSpy reopened(workspace, &QueryWorkspace::connectionReady);
        QTest::mouseClick(pins->viewport(), Qt::LeftButton, Qt::NoModifier,
                          pins->visualItemRect(pins->item(0)).center());
        QTRY_COMPARE(reopened.count(), 1);
        QTRY_COMPARE(tree->currentIndex().data(NavigatorModel::KindRole).toString(),
                     QString("table"));
        QCOMPARE(tree->currentIndex().data(NavigatorModel::QualifiedNameRole).toString(),
                 QString("\"main\".\"orders\""));
        QVERIFY(filter->text().isEmpty());
        QCOMPARE(selector->currentData(), editorTarget);
        auto renamedProfile = profiles->item(0)->data(Qt::UserRole).value<SavedProfile>();
        renamedProfile.name = "Alpha Renamed";
        workspace->adapter()->saveProfile(renamedProfile, 6002);
        QTRY_VERIFY(pins->item(0)->toolTip().contains("Alpha Renamed"));
        workspace->adapter()->deleteProfile(renamedProfile.id, 6003);
        QTRY_COMPARE(pins->count(), 0);
    }
};

QTEST_MAIN(PinningFlowTest)
#include "pinning_flow_test.moc"
