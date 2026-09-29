#include "app/main_window.h"
#include "app/navigator_controller.h"
#include "app/pin_store.h"
#include "app/pinned_tree_model.h"
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
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <algorithm>

namespace {
QModelIndex pinRoot(QTreeView* pins) {
    return pins->model()->index(0, 0);
}
int pinCount(QTreeView* pins) {
    return pins->model()->rowCount();
}
int pinRow(QTreeView* pins, const QString& key) {
    auto* model = qobject_cast<choscordb::PinnedTreeModel*>(pins->model());
    if (!model)
        return -1;
    for (int row = 0; row < model->rowCount(); ++row)
        if (model->pinKey(model->index(row, 0)) == key)
            return row;
    return -1;
}
void clickArrow(QTreeView* pins, const QModelIndex& index) {
    const auto rect = pins->visualRect(index);
    const auto arrow = QPoint(rect.left() - pins->indentation() / 2, rect.center().y());
    QTest::mouseClick(pins->viewport(), Qt::LeftButton, Qt::NoModifier, arrow);
}
void clickPin(QTreeView* pins) {
    const auto rect = pins->visualRect(pinRoot(pins));
    QTest::mouseClick(pins->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(rect.right() - 4, rect.center().y()));
}
} // namespace

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

class DescendantPinProbe : public QObject {
  public:
    bool sawPin = false;

  protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() != QEvent::Show)
            return false;
        auto* menu = qobject_cast<QMenu*>(object);
        auto* pin = menu ? menu->findChild<QAction*>("pinObject") : nullptr;
        if (pin && pin->isEnabled()) {
            sawPin = true;
            QTimer::singleShot(0, menu, [menu, pin] {
                pin->trigger();
                menu->close();
            });
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
        auto* pins = window.findChild<QTreeView*>("pinnedList");
        QVERIFY(workspace && profiles && pins);
        SavedProfile profile;
        profile.id = pin.profileId;
        profile.name = pin.profileName;
        profile.path = directory.filePath("database.sqlite");
        workspace->adapter()->saveProfile(profile, 6201);
        QTRY_COMPARE(profiles->count(), 1);
        QTRY_COMPARE(pinCount(pins), 1);
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
        clickPin(pins);
        QVERIFY(guardRaised);
        QTRY_VERIFY(window.findChild<QMessageBox*>("sidebarConnectionFailure"));
        QCOMPARE(opened.count(), 0);
        QCOMPARE(pinCount(pins), 1);
        workspace->setExternalWork(false);
        window.findChild<QMessageBox*>("sidebarConnectionFailure")->accept();
        clickPin(pins);
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
        auto* pins = window.findChild<QTreeView*>("pinnedList");
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
        QTRY_COMPARE(pinCount(pins), 1);

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
        QTRY_VERIFY(pinRoot(pins).data(Qt::DisplayRole).toString().contains("orders_new"));
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
        QTRY_VERIFY(pinRoot(pins).data(Qt::ToolTipRole).toString().contains("Unavailable"));
        QCOMPARE(pinCount(pins), 1);
    }

    void restoredHiddenIndexAndKeyPinsOpenTheirTableDetailPanes() {
        using namespace choscordb;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto storePath = directory.filePath("profiles.sqlite");
        const auto profileId = QStringLiteral("hidden-detail-profile");
        QList<PinRecord> saved;
        PinRecord mismatched;
        {
            MainWindow setup(nullptr, storePath);
            setup.show();
            auto* workspace = setup.findChild<QueryWorkspace*>();
            auto* profiles = setup.findChild<QListWidget*>("savedConnections");
            auto* navigator = setup.findChild<NavigatorController*>();
            QVERIFY(workspace && profiles && navigator);
            SavedProfile profile;
            profile.id = profileId;
            profile.name = "Hidden detail";
            profile.path = directory.filePath("database.sqlite");
            workspace->adapter()->saveProfile(profile, 6801);
            QTRY_COMPARE(profiles->count(), 1);
            QSignalSpy connected(workspace, &QueryWorkspace::connectionReady);
            QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                              profiles->visualItemRect(profiles->item(0)).center());
            QTRY_COMPARE(connected.count(), 1);
            const auto connection = connected.first().at(0).toULongLong();
            auto* adapter = workspace->adapter();
            int finished = 0;
            connect(adapter, &EngineAdapter::eventReady, &setup, [&](const BridgeEvent& event) {
                if (event.kind == "query_finished")
                    ++finished;
            });
            for (const auto& statement :
                 {"CREATE TABLE orders(id INTEGER PRIMARY KEY, note TEXT UNIQUE)",
                  "CREATE INDEX orders_note_ix ON orders(note)"}) {
                const auto query = adapter->execute(connection, statement);
                QVERIFY(query);
                adapter->fetchPage(*query);
                QTRY_COMPARE(
                    finished,
                    statement ==
                            QStringLiteral(
                                "CREATE TABLE orders(id INTEGER PRIMARY KEY, note TEXT UNIQUE)")
                        ? 1
                        : 2);
                adapter->releaseQuery(*query);
            }
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
            model->fetchMore(table);
            QTRY_VERIFY(table.data(NavigatorModel::ChildrenLoadedRole).toBool());
            for (int row = 0; row < model->rowCount(table); ++row) {
                const auto child = model->index(row, 0, table);
                const auto kind = child.data(NavigatorModel::KindRole).toString();
                if (!((kind == "index" &&
                       child.data(Qt::DisplayRole).toString() == "orders_note_ix") ||
                      kind == "primarykey"))
                    continue;
                PinRecord pin;
                pin.profileId = profileId;
                pin.profileName = profile.name;
                pin.objectId = child.data(NavigatorModel::ObjectIdRole).toString();
                pin.name = child.data(Qt::DisplayRole).toString();
                pin.qualifiedName = child.data(NavigatorModel::QualifiedNameRole).toString();
                pin.kind = kind;
                pin.parentObjectId = table.data(NavigatorModel::ObjectIdRole).toString();
                for (auto parent = child.parent(); parent.isValid(); parent = parent.parent())
                    if (parent.data(NavigatorModel::KindRole).toString() != "connection") {
                        pin.ancestryIds.prepend(
                            parent.data(NavigatorModel::ObjectIdRole).toString());
                        pin.ancestryNames.prepend(parent.data(Qt::DisplayRole).toString());
                    }
                saved.append(pin);
            }
            QCOMPARE(saved.size(), 2);
            std::sort(saved.begin(), saved.end(),
                      [](const PinRecord& left, const PinRecord& right) {
                          return left.kind == QLatin1String("index") &&
                                 right.kind != QLatin1String("index");
                      });
            mismatched = saved.first();
            mismatched.qualifiedName = QStringLiteral("main.replaced_ix");
            QString error;
            auto stored = saved;
            stored.append(mismatched);
            QVERIFY2(PinStore(storePath).save(stored, &error), qPrintable(error));
        }
        MainWindow restored(nullptr, storePath);
        restored.show();
        auto* pins = restored.findChild<QTreeView*>("pinnedList");
        auto* tree = restored.findChild<QTreeView*>("databaseNavigator");
        auto* tabs = restored.findChild<QTabWidget*>("editorTabs");
        auto* profiles = restored.findChild<QListWidget*>("savedConnections");
        QVERIFY(pins && tree && tabs && profiles);
        QTRY_COMPARE(pinCount(pins), 3);
        QTRY_COMPARE(profiles->count(), 1);
        for (const auto& pin : saved) {
            const auto row = pinRow(pins, PinStore::identityKey(pin));
            QVERIFY(row >= 0);
            const auto pinIndex = pins->model()->index(row, 0);
            const auto rect = pins->visualRect(pinIndex);
            QTest::mouseClick(pins->viewport(), Qt::LeftButton, Qt::NoModifier,
                              QPoint(rect.right() - 4, rect.center().y()));
            QTRY_COMPARE(tree->currentIndex().data(NavigatorModel::ObjectIdRole).toString(),
                         pin.parentObjectId);
            QTRY_VERIFY(tabs->currentWidget());
            QCOMPARE(tabs->currentWidget()->property("objectId").toString(), pin.parentObjectId);
            auto* panes = tabs->currentWidget()->findChild<QTabBar*>("objectTabs");
            QVERIFY(panes);
            QTRY_VERIFY2(panes->currentIndex() == (pin.kind == "index" ? 1 : 2),
                         qPrintable(QStringLiteral("kind=%1 pane=%2 status=%3")
                                        .arg(pin.kind)
                                        .arg(panes->currentIndex())
                                        .arg(pinIndex.data(Qt::ToolTipRole).toString())));
            QVERIFY(!pins->model()
                         ->index(pinRow(pins, PinStore::identityKey(pin)), 0)
                         .data(Qt::ToolTipRole)
                         .toString()
                         .contains("Unavailable"));
        }
        const auto mismatchedRow = pinRow(pins, PinStore::identityKey(mismatched));
        QVERIFY(mismatchedRow >= 0);
        const auto mismatchedIndex = pins->model()->index(mismatchedRow, 0);
        const auto mismatchedRect = pins->visualRect(mismatchedIndex);
        QTest::mouseClick(pins->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(mismatchedRect.right() - 4, mismatchedRect.center().y()));
        QTRY_VERIFY(pins->model()
                        ->index(pinRow(pins, PinStore::identityKey(mismatched)), 0)
                        .data(Qt::ToolTipRole)
                        .toString()
                        .contains("Unavailable"));
        QCOMPARE(pinCount(pins), 3);
        PinnedMenuProbe probe;
        qApp->installEventFilter(&probe);
        QVERIFY(QMetaObject::invokeMethod(
            pins, "customContextMenuRequested",
            Q_ARG(QPoint, pins->visualRect(pins->model()->index(mismatchedRow, 0)).center())));
        qApp->removeEventFilter(&probe);
        QVERIFY(probe.sawUnpin);
        QTRY_COMPARE(pinCount(pins), 2);
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
            auto* pins = window.findChild<QTreeView*>("pinnedList");
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
            QTRY_COMPARE(pinCount(pins), 1);
            QVERIFY(pinRoot(pins).data(Qt::DisplayRole).toString().contains("orders"));
            QMenu after;
            navigator->populateContextMenu(&after, table);
            QVERIFY(after.findChild<QAction*>("unpinObject"));
            const auto pinPoint = pins->visualRect(pinRoot(pins)).center();
            QCOMPARE(pins->indexAt(pinPoint), pinRoot(pins));
            PinnedMenuProbe probe;
            qApp->installEventFilter(&probe);
            QVERIFY(QMetaObject::invokeMethod(pins, "customContextMenuRequested",
                                              Q_ARG(QPoint, pinPoint)));
            qApp->removeEventFilter(&probe);
            QVERIFY(probe.sawUnpin);
            QTRY_COMPARE(pinCount(pins), 0);
            QVERIFY(!pinnedSection->isVisible());
            QCOMPARE(table.data(Qt::DisplayRole).toString(), QString("orders"));
            QMenu again;
            navigator->populateContextMenu(&again, table);
            auto* repin = again.findChild<QAction*>("pinObject");
            QVERIFY(repin && repin->isEnabled());
            repin->trigger();
            QTRY_COMPARE(pinCount(pins), 1);
            QTRY_VERIFY(pinnedSection->isVisible());
        }
        MainWindow restored(nullptr, store);
        restored.show();
        auto* pins = restored.findChild<QTreeView*>("pinnedList");
        auto* tree = restored.findChild<QTreeView*>("databaseNavigator");
        auto* filter = restored.findChild<QLineEdit*>("navigatorFilter");
        auto* profiles = restored.findChild<QListWidget*>("savedConnections");
        auto* selector = restored.findChild<QComboBox*>("connectionSelector");
        auto* workspace = restored.findChild<QueryWorkspace*>();
        QVERIFY(pins && tree && filter && profiles && selector && workspace);
        QTRY_COMPARE(pinCount(pins), 1);
        QCOMPARE(tree->model()->rowCount(), 0);
        QVERIFY(pinRoot(pins).data(Qt::ToolTipRole).toString().contains("Alpha"));
        QTRY_COMPARE(profiles->count(), 1);
        const auto editorTarget = selector->currentData();
        filter->setText("nothing_matches");
        QCOMPARE(pinCount(pins), 1);
        QSignalSpy reopened(workspace, &QueryWorkspace::connectionReady);
        clickPin(pins);
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
        QTRY_VERIFY(pinRoot(pins).data(Qt::ToolTipRole).toString().contains("Alpha Renamed"));
        workspace->adapter()->deleteProfile(renamedProfile.id, 6003);
        QTRY_COMPARE(pinCount(pins), 0);
    }

    void disconnectedPinArrowLoadsLiveChildrenWithoutRevealingOriginal() {
        using namespace choscordb;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto storePath = directory.filePath("profiles.sqlite");
        {
            MainWindow setup(nullptr, storePath);
            setup.show();
            auto* workspace = setup.findChild<QueryWorkspace*>();
            auto* profiles = setup.findChild<QListWidget*>("savedConnections");
            auto* navigator = setup.findChild<NavigatorController*>();
            QVERIFY(workspace && profiles && navigator);
            SavedProfile profile;
            profile.id = "expand-profile";
            profile.name = "Expand";
            profile.path = directory.filePath("database.sqlite");
            workspace->adapter()->saveProfile(profile, 7001);
            QTRY_COMPARE(profiles->count(), 1);
            QSignalSpy connected(workspace, &QueryWorkspace::connectionReady);
            QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                              profiles->visualItemRect(profiles->item(0)).center());
            QTRY_COMPARE(connected.count(), 1);
            const auto connection = connected.first().at(0).toULongLong();
            auto* adapter = workspace->adapter();
            int completed = 0;
            connect(adapter, &EngineAdapter::eventReady, &setup, [&](const BridgeEvent& event) {
                if (event.kind == "query_finished")
                    ++completed;
            });
            const auto query = adapter->execute(connection, "CREATE TABLE orders (id INTEGER)");
            QVERIFY(query);
            adapter->fetchPage(*query);
            QTRY_COMPARE(completed, 1);
            adapter->releaseQuery(*query);
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
            QCOMPARE(table.data(NavigatorModel::KindRole).toString(), QString("table"));
            QMenu menu;
            navigator->populateContextMenu(&menu, table);
            auto* pin = menu.findChild<QAction*>("pinObject");
            QVERIFY(pin && pin->isEnabled());
            pin->trigger();
            auto* pins = setup.findChild<QTreeView*>("pinnedList");
            QVERIFY(pins);
            QTRY_COMPARE(pinCount(pins), 1);
        }

        MainWindow restored(nullptr, storePath);
        restored.show();
        auto* workspace = restored.findChild<QueryWorkspace*>();
        auto* pins = restored.findChild<QTreeView*>("pinnedList");
        auto* explorer = restored.findChild<QTreeView*>("databaseNavigator");
        auto* filter = restored.findChild<QLineEdit*>("navigatorFilter");
        auto* tabs = restored.findChild<QTabWidget*>("editorTabs");
        QVERIFY(workspace && pins && explorer && filter && tabs);
        QTRY_COMPARE(pinCount(pins), 1);
        QTRY_VERIFY(pins->isVisible());
        const auto root = pinRoot(pins);
        QVERIFY(pins->model()->hasChildren(root));
        QVERIFY(!pins->isExpanded(root));
        QCOMPARE(explorer->model()->rowCount(), 0);
        QSignalSpy reopened(workspace, &QueryWorkspace::connectionReady);
        filter->setText("nothing_matches");
        const int tabCount = tabs->count();
        const auto arrow = QPoint(pins->indentation() / 2, pins->visualRect(root).center().y());
        workspace->setExternalWork(true);
        QTest::mouseClick(pins->viewport(), Qt::LeftButton, Qt::NoModifier, arrow);
        QTRY_VERIFY(pins->isExpanded(root));
        QTRY_VERIFY(root.data(Qt::ToolTipRole).toString().contains("retry", Qt::CaseInsensitive));
        QCOMPARE(reopened.count(), 0);
        QTest::mouseClick(pins->viewport(), Qt::LeftButton, Qt::NoModifier, arrow);
        QTRY_VERIFY(!pins->isExpanded(root));
        workspace->setExternalWork(false);
        QTest::mouseClick(pins->viewport(), Qt::LeftButton, Qt::NoModifier, arrow);
        QTRY_VERIFY(pins->isExpanded(root));
        QTRY_COMPARE(reopened.count(), 1);
        QTRY_VERIFY(pins->model()->rowCount(root) > 0);
        QCOMPARE(filter->text(), QString("nothing_matches"));
        QVERIFY(!explorer->currentIndex().isValid());
        QCOMPARE(tabs->count(), tabCount);
        const auto findColumn = [&] {
            for (int row = 0; row < pins->model()->rowCount(root); ++row) {
                const auto child = pins->model()->index(row, 0, root);
                if (child.data(NavigatorModel::KindRole).toString() == QStringLiteral("column"))
                    return child;
            }
            return QModelIndex();
        };
        QTRY_VERIFY(findColumn().isValid());
        const auto column = findColumn();
        QCOMPARE(column.data(Qt::DisplayRole).toString(), QString("id"));
        QTest::mouseClick(pins->viewport(), Qt::LeftButton, Qt::NoModifier,
                          pins->visualRect(column).center());
        QTRY_COMPARE(tabs->count(), tabCount + 1);
        QCOMPARE(pinCount(pins), 1);
        QString pinError;
        QCOMPARE(PinStore(storePath).load(&pinError).size(), 1);
        QVERIFY(pinError.isEmpty());
        QCOMPARE(filter->text(), QString("nothing_matches"));

        const auto connection = reopened.first().at(0).toULongLong();
        auto* adapter = workspace->adapter();
        int completed = 0;
        connect(adapter, &EngineAdapter::eventReady, &restored, [&](const BridgeEvent& event) {
            if (event.kind == "query_finished")
                ++completed;
        });
        const auto drop = adapter->execute(connection, "DROP TABLE orders");
        QVERIFY(drop);
        adapter->fetchPage(*drop);
        QTRY_COMPARE(completed, 1);
        adapter->releaseQuery(*drop);
        clickArrow(pins, root);
        QTRY_VERIFY(!pins->isExpanded(root));
        clickArrow(pins, root);
        QTRY_VERIFY(pins->isExpanded(root));
        QTRY_VERIFY(root.data(Qt::ToolTipRole).toString().contains("Unavailable"));
        QCOMPARE(pins->model()->rowCount(root), 0);
        QCOMPARE(pinCount(pins), 1);
    }

    void failedConnectionDoesNotCancelAnotherPendingPinExpansion() {
        using namespace choscordb;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto storePath = directory.filePath("profiles.sqlite");
        SavedProfile good;
        good.id = "good";
        good.name = "Working";
        good.path = directory.filePath("working.sqlite");
        {
            MainWindow setup(nullptr, storePath);
            setup.show();
            auto* workspace = setup.findChild<QueryWorkspace*>();
            auto* profiles = setup.findChild<QListWidget*>("savedConnections");
            QVERIFY(workspace && profiles);
            workspace->adapter()->saveProfile(good, 7101);
            QTRY_COMPARE(profiles->count(), 1);
            QSignalSpy connected(workspace, &QueryWorkspace::connectionReady);
            QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                              profiles->visualItemRect(profiles->item(0)).center());
            QTRY_COMPARE(connected.count(), 1);
            const auto connection = connected.first().at(0).toULongLong();
            auto* adapter = workspace->adapter();
            int completed = 0;
            connect(adapter, &EngineAdapter::eventReady, &setup, [&](const BridgeEvent& event) {
                if (event.kind == "query_finished")
                    ++completed;
            });
            const auto query = adapter->execute(connection, "CREATE TABLE orders (id INTEGER)");
            QVERIFY(query);
            adapter->fetchPage(*query);
            QTRY_COMPARE(completed, 1);
            adapter->releaseQuery(*query);
        }
        auto tablePin = [](const QString& profileId, const QString& profileName) {
            PinRecord pin;
            pin.profileId = profileId;
            pin.profileName = profileName;
            pin.objectId = QStringLiteral("[\"main\",\"orders\"]");
            pin.name = QStringLiteral("orders");
            pin.qualifiedName = QStringLiteral("\"main\".\"orders\"");
            pin.kind = QStringLiteral("table");
            pin.parentObjectId = QStringLiteral("[\"main\",\"group\",\"table\"]");
            pin.ancestryIds = {QStringLiteral("[\"main\"]"), pin.parentObjectId};
            pin.ancestryNames = {QStringLiteral("main"), QStringLiteral("Tables")};
            return pin;
        };
        QString error;
        QVERIFY2(PinStore(storePath).save({tablePin("bad", "Broken"), tablePin("good", "Working")},
                                          &error),
                 qPrintable(error));
        MainWindow window(nullptr, storePath);
        window.show();
        auto* workspace = window.findChild<QueryWorkspace*>();
        auto* profiles = window.findChild<QListWidget*>("savedConnections");
        auto* pins = window.findChild<QTreeView*>("pinnedList");
        auto* navigator = window.findChild<NavigatorController*>();
        QVERIFY(workspace && profiles && pins && navigator);
        SavedProfile bad;
        bad.id = "bad";
        bad.name = "Broken";
        bad.path = directory.path(); // Opening a directory as a database must fail.
        workspace->adapter()->saveProfile(bad, 7102);
        QTRY_COMPARE(profiles->count(), 2);
        QTRY_COMPARE(pinCount(pins), 2);
        QTRY_VERIFY(pins->isVisible());
        const auto badRoot = pins->model()->index(0, 0);
        const auto goodRoot = pins->model()->index(1, 0);
        QCOMPARE(goodRoot.data(Qt::ToolTipRole).toString().contains("Working"), true);
        auto* model = navigator->model();
        auto* adapter = workspace->adapter();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, adapter,
                            &EngineAdapter::loadMetadata);
        QSignalSpy metadata(model, &NavigatorModel::childrenRequested);
        QSignalSpy connected(workspace, &QueryWorkspace::connectionReady);
        clickArrow(pins, goodRoot);
        QTRY_VERIFY(pins->isExpanded(goodRoot));
        QTRY_COMPARE(connected.count(), 1);
        const auto goodConnection = connected.first().at(0).toULongLong();
        QTRY_VERIFY(metadata.count() > 0);
        clickArrow(pins, badRoot);
        QTRY_VERIFY(pins->isExpanded(badRoot));
        QTRY_VERIFY(
            badRoot.data(Qt::ToolTipRole).toString().contains("retry", Qt::CaseInsensitive));
        QVERIFY(goodRoot.data(Qt::ToolTipRole).toString().contains("Loading"));
        QVERIFY(pins->isExpanded(goodRoot));
        QModelIndex goodModelRoot;
        for (int row = 0; row < model->rowCount(); ++row) {
            const auto candidate = model->index(row, 0);
            if (candidate.data(NavigatorModel::ConnectionRole).toULongLong() == goodConnection)
                goodModelRoot = candidate;
        }
        QVERIFY(goodModelRoot.isValid());
        connect(model, &NavigatorModel::childrenRequested, &window,
                [adapter, goodConnection](quint64 connection, const QString& parent,
                                          quint64 requestToken) {
                    if (connection != goodConnection)
                        return;
                    QTimer::singleShot(0, adapter, [adapter, connection, parent, requestToken] {
                        adapter->loadMetadata(connection, parent, requestToken);
                    });
                });
        const auto token = model->pendingRequestToken(goodModelRoot);
        QVERIFY(token > 0);
        adapter->loadMetadata(goodConnection, {}, token);
        QTRY_VERIFY2(goodRoot.data(Qt::ToolTipRole).toString().contains("Connected"),
                     qPrintable(goodRoot.data(Qt::ToolTipRole).toString()));
        QTRY_VERIFY(pins->model()->rowCount(goodRoot) > 0);
        QCOMPARE(pinCount(pins), 2);
    }

    void expandingSiblingTablePinsKeepsTheFirstLiveSubtree() {
        using namespace choscordb;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto storePath = directory.filePath("profiles.sqlite");
        {
            MainWindow setup(nullptr, storePath);
            setup.show();
            auto* workspace = setup.findChild<QueryWorkspace*>();
            auto* profiles = setup.findChild<QListWidget*>("savedConnections");
            auto* navigator = setup.findChild<NavigatorController*>();
            auto* pins = setup.findChild<QTreeView*>("pinnedList");
            QVERIFY(workspace && profiles && navigator && pins);
            SavedProfile profile;
            profile.id = "siblings";
            profile.name = "Siblings";
            profile.path = directory.filePath("tables.sqlite");
            workspace->adapter()->saveProfile(profile, 7201);
            QTRY_COMPARE(profiles->count(), 1);
            QSignalSpy opened(workspace, &QueryWorkspace::connectionReady);
            QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                              profiles->visualItemRect(profiles->item(0)).center());
            QTRY_COMPARE(opened.count(), 1);
            const auto connection = opened.first().at(0).toULongLong();
            auto* adapter = workspace->adapter();
            int completed = 0;
            connect(adapter, &EngineAdapter::eventReady, &setup, [&](const BridgeEvent& event) {
                if (event.kind == "query_finished")
                    ++completed;
            });
            for (const auto& name : {"alpha", "beta"}) {
                const auto query = adapter->execute(
                    connection, QStringLiteral("CREATE TABLE %1 (id INTEGER)").arg(name));
                QVERIFY(query);
                adapter->fetchPage(*query);
                QTRY_COMPARE(completed, name == QStringLiteral("alpha") ? 1 : 2);
                adapter->releaseQuery(*query);
            }
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
            QCOMPARE(model->rowCount(group), 2);
            for (int row = 0; row < 2; ++row) {
                QMenu menu;
                navigator->populateContextMenu(&menu, model->index(row, 0, group));
                auto* pin = menu.findChild<QAction*>("pinObject");
                QVERIFY(pin && pin->isEnabled());
                pin->trigger();
            }
            QTRY_COMPARE(pinCount(pins), 2);
        }
        MainWindow restored(nullptr, storePath);
        restored.show();
        auto* pins = restored.findChild<QTreeView*>("pinnedList");
        auto* workspace = restored.findChild<QueryWorkspace*>();
        QVERIFY(pins && workspace);
        QTRY_COMPARE(pinCount(pins), 2);
        QTRY_VERIFY(pins->isVisible());
        const auto first = pins->model()->index(0, 0);
        const auto second = pins->model()->index(1, 0);
        QCOMPARE(first.data(Qt::DisplayRole).toString(), QString("beta"));
        QCOMPARE(second.data(Qt::DisplayRole).toString(), QString("alpha"));
        QSignalSpy reopened(workspace, &QueryWorkspace::connectionReady);
        clickArrow(pins, first);
        QTRY_VERIFY(pins->isExpanded(first));
        QTRY_COMPARE(reopened.count(), 1);
        QTRY_COMPARE(pins->model()->index(0, 0, first).data(Qt::DisplayRole).toString(),
                     QString("id"));
        clickArrow(pins, second);
        QTRY_VERIFY(pins->isExpanded(second));
        QTRY_COMPARE(pins->model()->index(0, 0, second).data(Qt::DisplayRole).toString(),
                     QString("id"));
        QVERIFY(pins->isExpanded(first));
        QCOMPARE(pins->model()->index(0, 0, first).data(Qt::DisplayRole).toString(), QString("id"));
        QCOMPARE(reopened.count(), 1);

        auto* navigator = restored.findChild<NavigatorController*>();
        QVERIFY(navigator);
        auto* model = navigator->model();
        auto* adapter = workspace->adapter();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, adapter,
                            &EngineAdapter::loadMetadata);
        QSignalSpy metadata(model, &NavigatorModel::childrenRequested);
        QModelIndex table;
        for (int rootRow = 0; rootRow < model->rowCount(); ++rootRow) {
            const auto connectionRoot = model->index(rootRow, 0);
            for (int schemaRow = 0; schemaRow < model->rowCount(connectionRoot); ++schemaRow) {
                const auto schema = model->index(schemaRow, 0, connectionRoot);
                for (int groupRow = 0; groupRow < model->rowCount(schema); ++groupRow) {
                    const auto group = model->index(groupRow, 0, schema);
                    for (int tableRow = 0; tableRow < model->rowCount(group); ++tableRow) {
                        const auto candidate = model->index(tableRow, 0, group);
                        if (candidate.data(NavigatorModel::KindRole).toString() ==
                                QStringLiteral("table") &&
                            candidate.data(Qt::DisplayRole).toString() == QStringLiteral("beta"))
                            table = candidate;
                    }
                }
            }
        }
        QVERIFY(table.isValid());
        const auto connection = table.data(NavigatorModel::ConnectionRole).toULongLong();
        model->refresh(table);
        QTRY_VERIFY(metadata.count() > 0);
        QVERIFY(model->applyChildren(connection,
                                     table.data(NavigatorModel::ObjectIdRole).toString(),
                                     metadata.last().at(2).toULongLong(),
                                     {{"nested-columns", "Columns", {}, "group", true}}));
        QTRY_COMPARE(pins->model()->index(0, 0, first).data(Qt::DisplayRole).toString(),
                     QString("Columns"));
        const auto nested = pins->model()->index(0, 0, first);
        QVERIFY(pins->model()->hasChildren(nested));
        clickArrow(pins, nested);
        QTRY_VERIFY(pins->isExpanded(nested));
        QTRY_COMPARE(metadata.count(), 2);
        QVERIFY(model->applyChildren(connection, "nested-columns",
                                     metadata.last().at(2).toULongLong(),
                                     {{"nested-id", "id", "main.beta.id", "column", false}}));
        QTRY_COMPARE(pins->model()->index(0, 0, nested).data(Qt::DisplayRole).toString(),
                     QString("id"));
        const auto column = pins->model()->index(0, 0, nested);
        DescendantPinProbe probe;
        qApp->installEventFilter(&probe);
        QVERIFY(QMetaObject::invokeMethod(pins, "customContextMenuRequested",
                                          Q_ARG(QPoint, pins->visualRect(column).center())));
        qApp->removeEventFilter(&probe);
        QVERIFY(probe.sawPin);
        QTRY_COMPARE(pinCount(pins), 3);
        QCOMPARE(pinRoot(pins).data(NavigatorModel::KindRole).toString(), QString("column"));
    }
};

QTEST_MAIN(PinningFlowTest)
#include "pinning_flow_test.moc"
