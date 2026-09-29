#include "app/navigator_controller.h"
#include "app/pin_store.h"
#include "bridge/engine_adapter.h"
#include "models/navigator_model.h"
#include <QAction>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QTreeView>
#include <QtTest>
using namespace choscordb;
class NavigatorPinModelTest : public QObject {
    Q_OBJECT
  private slots:
    void directRootSchemaOffersPinWhenSaved() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(25, "MySQL saved profile"));
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(
            25, {}, requested.last().at(2).toULongLong(),
            {{"mysql:schema:analytics", "analytics", "`analytics`", "schema", true}}));
        const auto schema = model->index(0, 0, root);
        controller.setPinStateResolver([](const QModelIndex& index) -> std::optional<bool> {
            PinRecord candidate;
            candidate.profileId = "saved-mysql";
            candidate.objectId = index.data(NavigatorModel::ObjectIdRole).toString();
            candidate.name = index.data(Qt::DisplayRole).toString();
            candidate.qualifiedName = index.data(NavigatorModel::QualifiedNameRole).toString();
            candidate.kind = index.data(NavigatorModel::KindRole).toString();
            candidate.parentObjectId = index.parent().data(NavigatorModel::ObjectIdRole).toString();
            return PinStore::valid(candidate) ? std::optional<bool>(false) : std::nullopt;
        });
        QMenu menu;
        controller.populateContextMenu(&menu, schema);
        auto* pin = menu.findChild<QAction*>("pinObject");
        QVERIFY(pin && pin->isEnabled());
    }

    void pinRevealReportsRejectedSelection() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(22, "Saved"));
        controller.setVisibleConnections({22});
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(22, {}, requested.last().at(2).toULongLong(),
                                     {{"target", "target", "main.target", "table", false}}));
        QObject::connect(tree.selectionModel(), &QItemSelectionModel::currentChanged, &tree,
                         [&tree](const QModelIndex&, const QModelIndex& previous) {
                             const QSignalBlocker blocked(tree.selectionModel());
                             tree.selectionModel()->setCurrentIndex(
                                 previous, QItemSelectionModel::ClearAndSelect);
                         });
        QList<NavigatorController::RevealResult> results;
        QVERIFY(controller.revealObject(22, {}, "target", "table", "main.target", {},
                                        [&](NavigatorController::RevealResult result,
                                            const QString&) { results.append(result); }));
        QTRY_COMPARE(requested.count(), 2); // Verify the cached target against fresh metadata.
        QVERIFY(model->applyChildren(22, {}, requested.last().at(2).toULongLong(),
                                     {{"target", "target", "main.target", "table", false}}));
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results.first(), NavigatorController::RevealResult::Retry);
        QVERIFY(!tree.currentIndex().isValid());
    }

    void canceledPinRevealCannotReplaceNewSelectionAfterDelayedMetadata() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(23, "Saved"));
        controller.setVisibleConnections({23});
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(23, {}, requested.last().at(2).toULongLong(),
                                     {{"group-a", "Group A", {}, "group", true},
                                      {"group-b", "Group B", {}, "group", true}}));
        const auto groupB = model->index(1, 0, root);
        model->fetchMore(groupB);
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model->applyChildren(23, "group-b", requested.last().at(2).toULongLong(),
                                     {{"b", "B", "main.B", "table", false}}));

        bool aIsPinnedAndActive = true;
        QList<NavigatorController::RevealResult> aResults;
        QVERIFY(controller.revealObject(
            23, {"group-a"}, "a", "table", "main.A", {},
            [&](NavigatorController::RevealResult result, const QString&) {
                aResults.append(result);
            },
            [&] { return aIsPinnedAndActive; }));
        QTRY_COMPARE(requested.count(), 3); // Group A's metadata is still delayed.
        const auto aToken = requested.last().at(2).toULongLong();

        aIsPinnedAndActive = false; // Unpin A, then activate B.
        QList<NavigatorController::RevealResult> bResults;
        QVERIFY(controller.revealObject(23, {"group-b"}, "b", "table", "main.B", {},
                                        [&](NavigatorController::RevealResult result,
                                            const QString&) { bResults.append(result); }));
        QTRY_COMPARE(requested.count(), 4);
        QVERIFY(model->applyChildren(23, "group-b", requested.last().at(2).toULongLong(),
                                     {{"b", "B", "main.B", "table", false}}));
        QTRY_COMPARE(bResults.size(), 1);
        QCOMPARE(bResults.first(), NavigatorController::RevealResult::Found);
        QCOMPARE(tree.currentIndex().data(NavigatorModel::ObjectIdRole).toString(), QString("b"));

        QVERIFY(
            model->applyChildren(23, "group-a", aToken, {{"a", "A", "main.A", "table", false}}));
        QTRY_COMPARE(aResults.size(), 1);
        QCOMPARE(aResults.first(), NavigatorController::RevealResult::Retry);
        QCOMPARE(tree.currentIndex().data(NavigatorModel::ObjectIdRole).toString(), QString("b"));
    }

    void cachedObjectIsRecheckedBeforePinCanSelectIt() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(26, "Saved"));
        controller.setVisibleConnections({26});
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(26, {}, requested.last().at(2).toULongLong(),
                                     {{"old", "old", "main.old", "table", false}}));
        QList<NavigatorController::RevealResult> outcomes;
        QVERIFY(controller.revealObject(26, {}, "old", "table", "main.old", {},
                                        [&](NavigatorController::RevealResult result,
                                            const QString&) { outcomes.append(result); }));
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model->applyChildren(26, {}, requested.last().at(2).toULongLong(), {}));
        QTRY_COMPARE(outcomes.size(), 1);
        QCOMPARE(outcomes.first(), NavigatorController::RevealResult::Unavailable);
        QVERIFY(!tree.currentIndex().isValid());
    }

    void inFlightPageCannotAuthorizeCachedPinTarget() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QObject::disconnect(model, &NavigatorModel::childrenPageRequested, &engine,
                            &EngineAdapter::loadMetadataPage);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QSignalSpy next(model, &NavigatorModel::childrenPageRequested);
        QVERIFY(model->addConnection(27, "Saved"));
        controller.setVisibleConnections({27});
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildrenPage(27, {}, requested.last().at(2).toULongLong(),
                                         {{"old", "old", "main.old", "table", false}}, 0, true, 1));
        model->requestNextPage(root);
        QTRY_COMPARE(next.count(), 1);
        const auto staleToken = next.last().at(2).toULongLong();
        QList<NavigatorController::RevealResult> outcomes;
        QVERIFY(controller.revealObject(27, {}, "old", "table", "main.old", {},
                                        [&](NavigatorController::RevealResult result,
                                            const QString&) { outcomes.append(result); }));
        QTRY_COMPARE(requested.count(), 2); // Activation discards the cached first page.
        QVERIFY(!tree.currentIndex().isValid());
        QVERIFY(!model->failChildren(27, {}, staleToken, "stale page failure"));
        QVERIFY(model->applyChildren(27, {}, requested.last().at(2).toULongLong(), {}));
        QTRY_COMPARE(outcomes.size(), 1);
        QCOMPARE(outcomes.first(), NavigatorController::RevealResult::Unavailable);
        QVERIFY(!tree.currentIndex().isValid());
    }

    void pinRevealKeepsFailedMetadataRetryableAndMarksOnlyCompleteAbsenceUnavailable() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(19, "Saved"));
        controller.setVisibleConnections({19});
        QList<NavigatorController::RevealResult> outcomes;
        QVERIFY(controller.revealObject(19, {}, "object", "table", "main.object", {},
                                        [&](NavigatorController::RevealResult result,
                                            const QString&) { outcomes.append(result); }));
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->failChildren(19, {}, requested.last().at(2).toULongLong(),
                                    "temporary catalog failure"));
        QTRY_COMPARE(outcomes.size(), 1);
        QCOMPARE(outcomes.first(), NavigatorController::RevealResult::Retry);
        outcomes.clear();
        model->refresh(model->index(0, 0));
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(controller.revealObject(19, {}, "object", "table", "main.object", {},
                                        [&](NavigatorController::RevealResult result,
                                            const QString&) { outcomes.append(result); }));
        QTRY_COMPARE(requested.count(), 3); // Reject the earlier pending reply.
        QVERIFY(model->applyChildren(19, {}, requested.last().at(2).toULongLong(),
                                     {{"different", "other", "main.other", "table", false}}));
        QTRY_COMPARE(outcomes.size(), 1);
        QCOMPARE(outcomes.first(), NavigatorController::RevealResult::Unavailable);
        QVERIFY(!tree.currentIndex().isValid());
    }

    void pinRevealLoadsPagesAndSelectsOnlyExactObject() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QObject::disconnect(model, &NavigatorModel::childrenPageRequested, &engine,
                            &EngineAdapter::loadMetadataPage);
        QSignalSpy first(model, &NavigatorModel::childrenRequested);
        QSignalSpy next(model, &NavigatorModel::childrenPageRequested);
        QVERIFY(model->addConnection(15, "Saved"));
        controller.setVisibleConnections({15});
        filter.setText("obstructing");
        bool completed = false;
        bool success = false;
        QVERIFY(controller.revealObject(
            15, {"db"}, "target", "table", "main.target", {},
            [&](NavigatorController::RevealResult result, const QString&) {
                completed = true;
                success = result == NavigatorController::RevealResult::Found;
            }));
        QTRY_COMPARE(first.count(), 1);
        QVERIFY(model->applyChildren(15, {}, first.last().at(2).toULongLong(),
                                     {{"db", "main", "main", "database", true}}));
        QTRY_COMPARE(first.count(), 3); // Reveal replaces the filter's in-flight load.
        QVERIFY(model->applyChildrenPage(15, "db", first.last().at(2).toULongLong(),
                                         {{"other", "other", "main.other", "table", false}}, 0,
                                         true, 1));
        QTRY_COMPARE(next.count(), 1);
        QVERIFY(model->applyChildrenPage(15, "db", next.last().at(2).toULongLong(),
                                         {{"target", "target", "main.target", "table", false}}, 1,
                                         false, 2));
        QTRY_VERIFY(completed);
        QVERIFY(success);
        QCOMPARE(filter.text(), QString());
        QCOMPARE(tree.currentIndex().data(NavigatorModel::ObjectIdRole).toString(),
                 QString("target"));
    }

    void hiddenTableIndexPinVerifiesFreshIdentityThenRevealsTable() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(31, "Saved"));
        controller.setVisibleConnections({31});
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(31, {}, requested.last().at(2).toULongLong(),
                                     {{"table", "orders", "main.orders", "table", true}}));
        const auto table = model->index(0, 0, root);
        model->fetchMore(table);
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model->applyChildren(31, "table", requested.last().at(2).toULongLong(),
                                     {{"index", "orders_ix", "main.orders_ix", "index", false}}));
        QCOMPARE(tree.model()->rowCount(tree.model()->index(0, 0, tree.model()->index(0, 0))),
                 0); // Hidden from the sidebar, still present in source metadata.
        QList<NavigatorController::RevealResult> outcomes;
        QVERIFY(controller.revealObject(31, {"table"}, "index", "index", "main.orders_ix", {},
                                        [&](NavigatorController::RevealResult result,
                                            const QString&) { outcomes.append(result); }));
        QTRY_COMPARE(requested.count(), 3);
        QVERIFY(!tree.currentIndex().isValid());
        QVERIFY(model->applyChildren(31, "table", requested.last().at(2).toULongLong(),
                                     {{"index", "orders_ix", "main.orders_ix", "index", false}}));
        QTRY_COMPARE(outcomes.size(), 1);
        QCOMPARE(outcomes.first(), NavigatorController::RevealResult::Found);
        QCOMPARE(tree.currentIndex().data(NavigatorModel::ObjectIdRole).toString(),
                 QString("table"));
        QCOMPARE(tree.currentIndex().data(NavigatorModel::KindRole).toString(), QString("table"));

        tree.setCurrentIndex({});
        outcomes.clear();
        QVERIFY(controller.revealObject(31, {"table"}, "index", "index", "main.orders_ix", {},
                                        [&](NavigatorController::RevealResult result,
                                            const QString&) { outcomes.append(result); }));
        QTRY_COMPARE(requested.count(), 4);
        QVERIFY(model->applyChildren(31, "table", requested.last().at(2).toULongLong(),
                                     {{"index", "orders_ix", "main.replaced_ix", "index", false}}));
        QTRY_COMPARE(outcomes.size(), 1);
        QCOMPARE(outcomes.first(), NavigatorController::RevealResult::Unavailable);
        QVERIFY(!tree.currentIndex().isValid());

        outcomes.clear();
        QVERIFY(controller.revealObject(31, {"table"}, "index", "index", "main.orders_ix", {},
                                        [&](NavigatorController::RevealResult result,
                                            const QString&) { outcomes.append(result); }));
        QTRY_COMPARE(requested.count(), 5);
        QVERIFY(outcomes.isEmpty()); // Loading is not confirmed absence.
        QVERIFY(model->failChildren(31, "table", requested.last().at(2).toULongLong(),
                                    "temporary catalog failure"));
        QTRY_COMPARE(outcomes.size(), 1);
        QCOMPARE(outcomes.first(), NavigatorController::RevealResult::Retry);
        QVERIFY(!tree.currentIndex().isValid());

        outcomes.clear();
        QVERIFY(controller.revealObject(31, {"table"}, "index", "index", "main.orders_ix", {},
                                        [&](NavigatorController::RevealResult result,
                                            const QString&) { outcomes.append(result); }));
        QTRY_COMPARE(requested.count(), 6);
        QVERIFY(model->applyChildren(31, "table", requested.last().at(2).toULongLong(), {}));
        QTRY_COMPARE(outcomes.size(), 1);
        QCOMPARE(outcomes.first(), NavigatorController::RevealResult::Unavailable);
        QVERIFY(!tree.currentIndex().isValid());
    }

    void schemaLevelIndexPinStillSelectsItsOwnRow() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(32, "Saved"));
        controller.setVisibleConnections({32});
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(32, {}, requested.last().at(2).toULongLong(),
                                     {{"group", "Indexes", {}, "group", true}}));
        const auto group = model->index(0, 0, root);
        model->fetchMore(group);
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model->applyChildren(32, "group", requested.last().at(2).toULongLong(),
                                     {{"index", "global_ix", "main.global_ix", "index", false}}));
        QList<NavigatorController::RevealResult> outcomes;
        QVERIFY(controller.revealObject(32, {"group"}, "index", "index", "main.global_ix", {},
                                        [&](NavigatorController::RevealResult result,
                                            const QString&) { outcomes.append(result); }));
        QTRY_COMPARE(requested.count(), 3);
        QVERIFY(model->applyChildren(32, "group", requested.last().at(2).toULongLong(),
                                     {{"index", "global_ix", "main.global_ix", "index", false}}));
        QTRY_COMPARE(outcomes.size(), 1);
        QCOMPARE(outcomes.first(), NavigatorController::RevealResult::Found);
        QCOMPARE(tree.currentIndex().data(NavigatorModel::ObjectIdRole).toString(),
                 QString("index"));
        QCOMPARE(tree.currentIndex().data(NavigatorModel::KindRole).toString(), QString("index"));
    }

    void pinMenuUsesSavedProfileAndExcludesNavigationRows() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(12, "Saved"));
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(12, {}, requested.last().at(2).toULongLong(),
                                     {{"db", "database", "database", "database", true}}));
        const auto database = model->index(0, 0, root);
        model->fetchMore(database);
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model->applyChildren(12, "db", requested.last().at(2).toULongLong(),
                                     {{"schema", "main", "main", "schema", true}}));
        const auto schema = model->index(0, 0, database);
        model->fetchMore(schema);
        QTRY_COMPARE(requested.count(), 3);
        QVERIFY(model->applyChildren(12, "schema", requested.last().at(2).toULongLong(),
                                     {{"group", "Tables", {}, "tables", true},
                                      {"table", "orders", "main.orders", "table", false}}));
        const auto group = model->index(0, 0, schema);
        const auto table = model->index(1, 0, schema);
        QSignalSpy pins(&controller, &NavigatorController::pinRequested);
        controller.setPinStateResolver(
            [](const QModelIndex&) -> std::optional<bool> { return false; });
        for (const auto excluded : {root, database, group}) {
            QMenu menu;
            controller.populateContextMenu(&menu, excluded);
            QVERIFY(!menu.findChild<QAction*>("pinObject"));
        }
        for (const auto eligible : {schema, table}) {
            QMenu menu;
            controller.populateContextMenu(&menu, eligible);
            auto* pin = menu.findChild<QAction*>("pinObject");
            QVERIFY(pin && pin->isEnabled());
            pin->trigger();
        }
        QCOMPARE(pins.count(), 2);
        QCOMPARE(pins.last().at(0).toModelIndex(), table);
        QCOMPARE(pins.last().at(1).toBool(), false);
        controller.setPinStateResolver(
            [](const QModelIndex&) -> std::optional<bool> { return true; });
        QMenu pinnedMenu;
        controller.populateContextMenu(&pinnedMenu, table);
        auto* unpin = pinnedMenu.findChild<QAction*>("unpinObject");
        QVERIFY(unpin && unpin->isEnabled());
        unpin->trigger();
        QCOMPARE(pins.last().at(1).toBool(), true);
        controller.setPinStateResolver(
            [](const QModelIndex&) -> std::optional<bool> { return std::nullopt; });
        QMenu unsavedMenu;
        controller.populateContextMenu(&unsavedMenu, table);
        auto* unavailable = unsavedMenu.findChild<QAction*>("pinObject");
        QVERIFY(unavailable && !unavailable->isEnabled());
        QVERIFY(unavailable->toolTip().contains("sav", Qt::CaseInsensitive));
    }
};
QTEST_MAIN(NavigatorPinModelTest)
#include "navigator_pin_model_test.moc"
