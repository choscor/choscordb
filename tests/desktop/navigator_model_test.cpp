#include "app/navigator_controller.h"
#include "app/pin_store.h"
#include "bridge/engine_adapter.h"
#include "models/navigator_model.h"
#include <QAbstractItemModelTester>
#include <QAction>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QTreeView>
#include <QVariantMap>
#include <QtTest>
using namespace choscordb;
class NavigatorModelTest : public QObject {
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

    void mutationMenuTargetsClickedObjectAndExplainsUnsupportedRename() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        controller.setDriverResolver([](quint64) { return QStringLiteral("sqlite"); });
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(7, "fixture"));
        controller.setSelectedConnection(7);
        auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(7, {}, requested.last().at(2).toULongLong(),
                                     {{"schema-id", "main", "\"main\"", "schema", true}}));
        const auto schema = model->index(0, 0, root);
        model->fetchMore(schema);
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model->applyChildren(
            7, "schema-id", requested.last().at(2).toULongLong(),
            {{"table-id", "Odd.table", "\"main\".\"Odd.table\"", "table", false},
             {"view-id", "A view", "\"main\".\"A view\"", "view", false},
             {"index-id", "idx", "\"main\".\"idx\"", "index", false},
             {"column-id", "col", "\"main\".\"col\"", "column", false}}));
        const auto table = model->index(0, 0, schema);
        const auto view = model->index(1, 0, schema);
        QSignalSpy actions(&controller, &NavigatorController::objectActionRequested);
        tree.setCurrentIndex(tree.model()->index(0, 0)); // Selection differs from clicked node.

        QMenu tableMenu;
        controller.populateContextMenu(&tableMenu, table);
        auto* dropTable = tableMenu.findChild<QAction*>("dropObject");
        auto* renameTable = tableMenu.findChild<QAction*>("renameObject");
        QVERIFY(dropTable && dropTable->isEnabled());
        QVERIFY(renameTable && renameTable->isEnabled());
        renameTable->trigger();
        QCOMPARE(actions.count(), 1);
        QCOMPARE(actions.last().at(0).toString(), QString("rename"));
        QCOMPARE(actions.last().at(1).toULongLong(), quint64(7));
        QCOMPARE(actions.last().at(2).toString(), QString("table-id"));
        QCOMPARE(actions.last().at(3).toString(), QString("Odd.table"));
        QCOMPARE(actions.last().at(4).toString(), QString("table"));
        QCOMPARE(actions.last().at(5).toString(), QString("schema-id"));
        QCOMPARE(actions.last().at(6).toString(), QString("\"main\".\"Odd.table\""));
        QCOMPARE(actions.last().at(7).toString(), QString());
        dropTable->trigger();
        QCOMPARE(actions.count(), 2);
        QCOMPARE(actions.last().at(0).toString(), QString("drop"));

        QMenu viewMenu;
        controller.populateContextMenu(&viewMenu, view);
        auto* dropView = viewMenu.findChild<QAction*>("dropObject");
        auto* renameView = viewMenu.findChild<QAction*>("renameObject");
        QVERIFY(dropView && dropView->isEnabled());
        QVERIFY(renameView && !renameView->isEnabled());
        QVERIFY(renameView->toolTip().contains("SQLite", Qt::CaseInsensitive));
        renameView->trigger();
        QCOMPARE(actions.count(), 2);
        dropView->trigger();
        QCOMPARE(actions.count(), 3);
        QCOMPARE(actions.last().at(2).toString(), QString("view-id"));

        for (const auto node : {schema, model->index(2, 0, schema), model->index(3, 0, schema)}) {
            QMenu otherMenu;
            controller.populateContextMenu(&otherMenu, node);
            QVERIFY(!otherMenu.findChild<QAction*>("dropObject"));
            QVERIFY(!otherMenu.findChild<QAction*>("renameObject"));
        }

        model->refresh(schema);
        renameTable->trigger();
        QCOMPARE(actions.count(), 3); // The old menu cannot act on removed metadata.
    }

    void viewRenameIsAvailableForPostgresAndMysql() {
        for (const auto& driver : {QStringLiteral("postgres"), QStringLiteral("mysql")}) {
            EngineAdapter engine;
            QTreeView tree;
            QLineEdit filter;
            NavigatorController controller(&engine, &tree, &filter, &tree);
            controller.setDriverResolver([driver](quint64) { return driver; });
            auto* model = controller.model();
            QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                                &EngineAdapter::loadMetadata);
            QSignalSpy requested(model, &NavigatorModel::childrenRequested);
            QVERIFY(model->addConnection(8, "fixture"));
            const auto root = model->index(0, 0);
            model->fetchMore(root);
            QTRY_COMPARE(requested.count(), 1);
            QVERIFY(model->applyChildren(8, {}, requested.last().at(2).toULongLong(),
                                         {{"view-id", "v", "\"v\"", "view", false}}));
            QMenu menu;
            controller.populateContextMenu(&menu, model->index(0, 0, root));
            auto* drop = menu.findChild<QAction*>("dropObject");
            auto* rename = menu.findChild<QAction*>("renameObject");
            QVERIFY(drop && drop->isEnabled());
            QVERIFY(rename && rename->isEnabled());
        }
    }

    void postgresRelationSubtypesKeepActionsAndReachTheRequest() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        controller.setDriverResolver([](quint64) { return QStringLiteral("postgres"); });
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(9, "fixture"));
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        const auto subtype = [](const QString& value) -> QVariantList {
            return {QVariantMap{{"name", "Relation subtype"}, {"value", value}}};
        };
        QVERIFY(
            model->applyChildren(9, {}, requested.last().at(2).toULongLong(),
                                 {{"table", "ordinary_table", "\"ordinary_table\"", "table", false},
                                  {"view", "ordinary_view", "\"ordinary_view\"", "view", false},
                                  {"foreign", "remote_table", "\"remote_table\"", "table", false,
                                   subtype("foreign_table")},
                                  {"materialized", "cached_view", "\"cached_view\"", "view", false,
                                   subtype("materialized_view")}}));
        QSignalSpy actions(&controller, &NavigatorController::objectActionRequested);
        const QStringList expectedSubtypes{QString{}, QString{}, QStringLiteral("foreign_table"),
                                           QStringLiteral("materialized_view")};
        for (int row = 0; row < 4; ++row) {
            QMenu menu;
            controller.populateContextMenu(&menu, model->index(row, 0, root));
            auto* drop = menu.findChild<QAction*>("dropObject");
            auto* rename = menu.findChild<QAction*>("renameObject");
            QVERIFY(drop && drop->isEnabled());
            QVERIFY(rename && rename->isEnabled());
            rename->trigger();
            QCOMPARE(actions.count(), 2 * row + 1);
            QCOMPARE(actions.last().at(0).toString(), QString("rename"));
            QCOMPARE(actions.last().at(7).toString(), expectedSubtypes.at(row));
            drop->trigger();
            QCOMPARE(actions.count(), 2 * row + 2);
            QCOMPARE(actions.last().at(0).toString(), QString("drop"));
            QCOMPARE(actions.last().at(7).toString(), expectedSubtypes.at(row));
        }
    }

    void metadataPagesAppendOnlyAfterExplicitContinuation() {
        NavigatorModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        tester.setUseFetchMore(false);
        QSignalSpy first(&model, &NavigatorModel::childrenRequested);
        QSignalSpy next(&model, &NavigatorModel::childrenPageRequested);
        QVERIFY(model.addConnection(1, "MySQL"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(first.count(), 1);
        QVERIFY(model.applyChildrenPage(1, {}, first.last().at(2).toULongLong(),
                                        {{"a", "Alpha", "db.Alpha", "table", false}}, 0, true, 1));
        QCOMPARE(model.rowCount(root), 2);
        const QPersistentModelIndex alpha(model.index(0, 0, root));
        QCOMPARE(model.index(1, 0, root).data(NavigatorModel::KindRole).toString(),
                 QString("load_more"));
        QVERIFY(!model.canFetchMore(root));
        QVERIFY(!root.data(NavigatorModel::ChildrenLoadedRole).toBool());
        QVERIFY(model.completionSnapshot(1, 100, 4096).partial);
        model.requestNextPage(model.index(1, 0, root));
        QTRY_COMPARE(next.count(), 1);
        QCOMPARE(next.last().at(3).toULongLong(), quint64(1));
        QCOMPARE(next.last().at(4).toUInt(), quint32(1000));
        QVERIFY(alpha.isValid());
        QVERIFY(model.applyChildrenPage(1, {}, next.last().at(2).toULongLong(),
                                        {{"b", "Beta", "db.Beta", "table", false}}, 1, false, 2));
        QVERIFY(alpha.isValid());
        QCOMPARE(alpha.data().toString(), QString("Alpha"));
        QCOMPARE(model.rowCount(root), 2);
        QCOMPARE(model.index(1, 0, root).data().toString(), QString("Beta"));
        QVERIFY(root.data(NavigatorModel::ChildrenLoadedRole).toBool());
        QVERIFY(!model.completionSnapshot(1, 100, 4096).partial);
        model.requestNextPage(root);
        QCOMPARE(next.count(), 1);
    }

    void repeatedIndexInLaterPageFailsWithoutDiscardingLoadedRows() {
        NavigatorModel model;
        QSignalSpy first(&model, &NavigatorModel::childrenRequested);
        QSignalSpy next(&model, &NavigatorModel::childrenPageRequested);
        QVERIFY(model.addConnection(1, "MySQL"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(first.count(), 1);
        QVERIFY(model.applyChildrenPage(1, {}, first.last().at(2).toULongLong(),
                                        {{"idx", "Index", "db.Index", "index", false}}, 0, true,
                                        1));
        const QPersistentModelIndex firstIndex(model.index(0, 0, root));
        model.requestNextPage(root);
        QTRY_COMPARE(next.count(), 1);
        QVERIFY(!model.applyChildrenPage(1, {}, next.last().at(2).toULongLong(),
                                         {{"idx", "Index", "db.Index", "index", false}}, 1, false,
                                         2));
        QVERIFY(firstIndex.isValid());
        QCOMPARE(firstIndex.data().toString(), QString("Index"));
        QCOMPARE(model.index(1, 0, root).data(NavigatorModel::KindRole).toString(),
                 QString("error"));
    }

    void refreshRejectsInFlightContinuationAndRestartsAtFirstPage() {
        NavigatorModel model;
        QSignalSpy first(&model, &NavigatorModel::childrenRequested);
        QSignalSpy next(&model, &NavigatorModel::childrenPageRequested);
        QVERIFY(model.addConnection(1, "MySQL"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(first.count(), 1);
        QVERIFY(model.applyChildrenPage(1, {}, first.last().at(2).toULongLong(),
                                        {{"a", "Alpha", "db.Alpha", "table", false}}, 0, true, 1));
        model.requestNextPage(root);
        QTRY_COMPARE(next.count(), 1);
        const auto oldToken = next.last().at(2).toULongLong();
        model.refresh(root);
        QTRY_COMPARE(first.count(), 2);
        QVERIFY(!model.applyChildrenPage(1, {}, oldToken,
                                         {{"b", "Beta", "db.Beta", "table", false}}, 1, false, 2));
        QVERIFY(model.applyChildrenPage(1, {}, first.last().at(2).toULongLong(),
                                        {{"c", "Current", "db.Current", "table", false}}, 0, false,
                                        1));
        QCOMPARE(model.rowCount(root), 1);
        QCOMPARE(model.index(0, 0, root).data().toString(), QString("Current"));
    }

    void moreThanTenThousandObjectsRemainBrowsableInBoundedPages() {
        NavigatorModel model;
        QSignalSpy first(&model, &NavigatorModel::childrenRequested);
        QSignalSpy next(&model, &NavigatorModel::childrenPageRequested);
        QVERIFY(model.addConnection(1, "MySQL"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(first.count(), 1);
        quint64 token = first.last().at(2).toULongLong();
        for (quint64 offset = 0; offset < 10001; offset += 1000) {
            std::vector<NavigatorObject> page;
            const quint64 end = qMin(offset + 1000, quint64(10001));
            for (quint64 i = offset; i < end; ++i) {
                const auto name = QString("table_%1").arg(i);
                page.push_back({name, name, "db." + name, "table", false});
            }
            QVERIFY(
                model.applyChildrenPage(1, {}, token, std::move(page), offset, end < 10001, end));
            if (end < 10001) {
                QVERIFY(!model.canFetchMore(root));
                model.requestNextPage(root);
                QTRY_COMPARE(next.count(), int(offset / 1000 + 1));
                QCOMPARE(next.last().at(3).toULongLong(), end);
                token = next.last().at(2).toULongLong();
            }
        }
        QCOMPARE(model.rowCount(root), 10001);
        QCOMPARE(model.index(10000, 0, root).data().toString(), QString("table_10000"));
        QVERIFY(root.data(NavigatorModel::ChildrenLoadedRole).toBool());
    }

    void sameIndexCanAppearInTableAndSchemaGroup() {
        NavigatorModel model;
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(1, "db"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model.applyChildren(
            1, {}, requested.last().at(2).toULongLong(),
            {{"table", "t", "t", "table", true}, {"group", "Indexes", {}, "group", true}}));
        auto table = model.index(0, 0, root);
        auto group = model.index(1, 0, root);
        model.fetchMore(table);
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model.applyChildren(1, "table", requested.last().at(2).toULongLong(),
                                    {{"index-id", "idx", "idx", "index", false}}));
        model.fetchMore(group);
        QTRY_COMPARE(requested.count(), 3);
        QVERIFY(model.applyChildren(1, "group", requested.last().at(2).toULongLong(),
                                    {{"index-id", "idx", "idx", "index", false}}));
        QCOMPARE(model.index(0, 0, group).data().toString(), QString("idx"));
    }
    void failedChildrenStayVisibleUntilExplicitRefresh() {
        NavigatorModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        tester.setUseFetchMore(false);
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(7, "Disconnected database"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        const auto failedToken = requested.last().at(2).toULongLong();
        QVERIFY(model.failChildren(7, {}, failedToken, "Connection is disconnected"));
        const QPersistentModelIndex failure(model.index(0, 0, root));
        QVERIFY(failure.data().toString().contains("refresh to retry"));
        QVERIFY(!model.canFetchMore(root));
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        model.fetchMore(root); // A view probing again must not remove its error row.
        bool nextTurn = false;
        QTimer::singleShot(0, &model, [&nextTurn] { nextTurn = true; });
        QTRY_VERIFY(nextTurn);
        QCOMPARE(requested.count(), 1);
        QCOMPARE(removed.count(), 0);
        QVERIFY(failure.isValid());
        QCOMPARE(failure.data(NavigatorModel::KindRole).toString(), QString("error"));
        QCOMPARE(root.data(NavigatorModel::ErrorRole).toString(),
                 QString("Connection is disconnected"));
        model.refresh(failure);
        QTRY_COMPARE(requested.count(), 2);
        const auto retryToken = requested.last().at(2).toULongLong();
        QVERIFY(retryToken > failedToken);
        QVERIFY(!failure.isValid());
        QVERIFY(!model.failChildren(7, {}, failedToken, "Late error"));
        QVERIFY(!model.applyChildren(7, {}, failedToken, {}));
        QVERIFY(model.applyChildren(
            7, {}, retryToken, {{"customers", "Customers", "public.customers", "table", false}}));
        QCOMPARE(model.index(0, 0, root).data().toString(), QString("Customers"));
        QVERIFY(root.data(NavigatorModel::ErrorRole).toString().isEmpty());
        QVERIFY(root.data(NavigatorModel::ChildrenLoadedRole).toBool());
    }
    void expandingThroughRecursiveProxyHandlesImmediateMetadata() {
        NavigatorModel model;
        QVERIFY(model.addConnection(1, "Database"));
        QSortFilterProxyModel proxy;
        proxy.setRecursiveFilteringEnabled(true);
        proxy.setSourceModel(&model);
        QTreeView tree;
        tree.setModel(&proxy);
        tree.resize(400, 300);
        tree.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tree));
        connect(&model, &NavigatorModel::childrenRequested, &model,
                [&model](quint64 connection, const QString& parent, quint64 token) {
                    std::vector<NavigatorObject> children;
                    for (int i = 0; i < 1000; ++i)
                        children.push_back({QString::number(i), QString::number(i),
                                            QString::number(i), "table", false});
                    QVERIFY(model.applyChildren(connection, parent, token, std::move(children)));
                });
        QCoreApplication::processEvents();
        const auto root = proxy.index(0, 0);
        QVERIFY(root.isValid());
        tree.expand(root);
        QCoreApplication::processEvents();
        QVERIFY(tree.isExpanded(root));
        QCOMPARE(proxy.rowCount(root), 1000);
    }
    void removedNodeCancelsItsDeferredRequest() {
        NavigatorModel model;
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(1, "Database"));
        model.fetchMore(model.index(0, 0));
        QVERIFY(model.removeConnection(1));
        bool eventTurnCompleted = false;
        QTimer::singleShot(0, &model, [&eventTurnCompleted] { eventTurnCompleted = true; });
        QTRY_VERIFY(eventTurnCompleted);
        QCOMPARE(requested.count(), 0);
    }
    void loadedColumnStateIsInvalidatedBeforeRefresh() {
        choscordb::NavigatorModel model;
        QSignalSpy requested(&model, &choscordb::NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(1, "Database"));
        const auto root = model.index(0, 0);
        QVERIFY(!root.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model.applyChildren(1, {}, requested.last().at(2).toULongLong(), {}));
        QVERIFY(root.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
        model.refresh(root);
        QVERIFY(!root.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
    }
    void completionCountsDatabaseUnicodeBytesAndBoundsIgnoredNodeTraversal() {
        NavigatorModel model;
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        model.addConnection(1, "one");
        model.fetchMore(model.index(0, 0));
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model.applyChildren(
            1, "", requested.last().at(2).toULongLong(),
            {{"a", QString::fromUtf8("😀"), QString::fromUtf8("😀"), "database", false}}));
        QVERIFY(model.completionSnapshot(1, 10, 16).objects.empty());
        const auto exact = model.completionSnapshot(1, 10, 17);
        QCOMPARE(exact.objects.size(), size_t(1));
        QVERIFY(!exact.partial);
        model.refresh(model.index(0, 0));
        QTRY_COMPARE(requested.count(), 2);
        std::vector<NavigatorObject> nodes;
        for (int i = 0; i < 100; ++i)
            nodes.push_back({QString::number(i), "ignored", "ignored", "index", false});
        nodes.push_back({"table", "last", "last", "table", false});
        QVERIFY(model.applyChildren(1, "", requested.last().at(2).toULongLong(), std::move(nodes)));
        const auto bounded = model.completionSnapshot(1, 1, 4096);
        QVERIFY(bounded.objects.empty());
        QVERIFY(bounded.partial);
    }
    void completionUsesAcceptedConnectionIsolatedMetadataAndInvalidates() {
        NavigatorModel model;
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QSignalSpy changed(&model, &NavigatorModel::completionChanged);
        model.addConnection(1, "one");
        model.addConnection(2, "two");
        auto first = model.index(0, 0);
        auto second = model.index(1, 0);
        model.fetchMore(first);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model.completionSnapshot(1, 100, 4096).objects.empty());
        QVERIFY(model.applyChildren(
            1, "", requested.last().at(2).toULongLong(),
            {{"a", "alpha", "alpha", "table", true}, {"index", "index", "index", "index", false}}));
        model.fetchMore(second);
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model.applyChildren(2, "", requested.last().at(2).toULongLong(),
                                    {{"b", "beta", "beta", "view", false}}));
        auto snapshot = model.completionSnapshot(1, 100, 4096);
        QCOMPARE(snapshot.objects.size(), size_t(1));
        QCOMPARE(snapshot.objects[0].name, QString("alpha"));
        QVERIFY(snapshot.partial); // Columns under the accepted table are not loaded.
        QCOMPARE(changed.count(), 2);
        const auto table = model.index(0, 0, first);
        model.fetchMore(table);
        QTRY_COMPARE(requested.count(), 3);
        const auto stale = requested.last().at(2).toULongLong();
        model.refresh(first);
        QTRY_COMPARE(requested.count(), 4);
        QVERIFY(model.completionSnapshot(1, 100, 4096).objects.empty());
        QCOMPARE(changed.count(), 3);
        QVERIFY(!model.applyChildren(1, "a", stale, {{"c", "col", "alpha.col", "column", false}}));
        QCOMPARE(changed.count(), 3);
        model.removeConnection(1);
        QCOMPARE(changed.count(), 4);
        QCOMPARE(model.completionSnapshot(2, 100, 4096).objects[0].name, QString("beta"));
    }
    void completionHonorsEntriesBytesAndDoesNotRetainExcessStringCapacity() {
        NavigatorModel model;
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        model.addConnection(1, "one");
        model.fetchMore(model.index(0, 0));
        QTRY_COMPARE(requested.count(), 1);
        QString bloated(100000, QChar('x'));
        bloated.resize(1);
        QVERIFY(model.applyChildren(
            1, "", requested.last().at(2).toULongLong(),
            {{"a", bloated, "x", "table", false}, {"b", "y", "y", "view", false}}));
        auto snapshot = model.completionSnapshot(1, 1, 4096);
        QCOMPARE(snapshot.objects.size(), size_t(1));
        QVERIFY(snapshot.partial);
        QVERIFY(snapshot.objects[0].name.capacity() < 100);
        snapshot = model.completionSnapshot(1, 100, 1);
        QVERIFY(snapshot.objects.empty());
        QVERIFY(snapshot.partial);
    }
    void hiddenPostgresSchemasDoNotConsumeCompletionVisitBudget() {
        NavigatorModel model;
        model.setDriverResolver([](quint64) { return QStringLiteral("postgres"); });
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        model.addConnection(1, "PostgreSQL");
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model.applyChildren(1, {}, requested.last().at(2).toULongLong(),
                                    {{"db", "database", "database", "database", true}}));
        const auto database = model.index(0, 0, root);
        model.fetchMore(database);
        QTRY_COMPARE(requested.count(), 2);
        std::vector<NavigatorObject> schemas;
        for (int i = 0; i < 100; ++i) {
            const auto name = QStringLiteral("pg_temp_%1").arg(i);
            schemas.push_back({QString::number(i), name, name, "schema", false});
        }
        schemas.push_back({"public", "public", "public", "schema", false});
        QVERIFY(
            model.applyChildren(1, "db", requested.last().at(2).toULongLong(), std::move(schemas)));
        const auto snapshot = model.completionSnapshot(1, 2, 4096);
        QCOMPARE(snapshot.objects.size(), size_t(2));
        QCOMPARE(snapshot.objects[1].name, QString("public"));
        QVERIFY(!snapshot.partial);
    }
    void refreshKeepsSiblingIndexesAndRejectsStaleResults() {
        NavigatorModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        tester.setUseFetchMore(false);
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(7, "test"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model.applyChildren(
            7, "", requested.last().at(2).toULongLong(),
            {{"a", "A", "A", "schema", true}, {"b", "B", "B", "schema", true}}));
        const QPersistentModelIndex a(model.index(0, 0, root)), b(model.index(1, 0, root));
        model.fetchMore(a);
        QTRY_COMPARE(requested.count(), 2);
        const auto old = requested.last().at(2).toULongLong();
        model.refresh(a);
        QTRY_COMPARE(requested.count(), 3);
        const auto fresh = requested.last().at(2).toULongLong();
        QVERIFY(fresh != old);
        QVERIFY(!model.applyChildren(7, "a", old, {{"stale", "stale", "stale", "table", false}}));
        QVERIFY(model.applyChildren(7, "a", fresh, {{"new", "new", "new", "table", false}}));
        QVERIFY(a.isValid());
        QVERIFY(b.isValid());
        QCOMPARE(model.data(b).toString(), QString("B"));
        QCOMPARE(model.data(model.index(0, 0, a)).toString(), QString("new"));
        QCOMPARE(model.rowCount(b), 0);
        QCOMPARE(requested.count(), 3);
    }
    void errorsArePlainAndRetryableAndRemovedRepliesAreIgnored() {
        NavigatorModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        tester.setUseFetchMore(false);
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(0, "<b>untrusted</b>"));
        const auto root = model.index(0, 0);
        QCOMPARE(model.data(root).toString(), QString("<b>untrusted</b>"));
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        const auto old = requested.last().at(2).toULongLong();
        QVERIFY(model.failChildren(0, "", old, "<script>error</script>"));
        QVERIFY(!model.canFetchMore(root));
        QCOMPARE(model.data(root, NavigatorModel::ErrorRole).toString(),
                 QString("<script>error</script>"));
        const auto failure = model.index(0, 0, root);
        QCOMPARE(model.data(failure, NavigatorModel::KindRole).toString(), QString("error"));
        QVERIFY(!model.data(failure, Qt::ToolTipRole).isValid());
        model.refresh(failure);
        QTRY_COMPARE(requested.count(), 2);
        const auto retry = requested.last().at(2).toULongLong();
        QVERIFY(retry != old);
        QCOMPARE(model.data(root, NavigatorModel::ErrorRole).toString(), QString());
        QVERIFY(!model.failChildren(0, "", old, "late"));
        QVERIFY(model.removeConnection(0));
        QVERIFY(!model.applyChildren(0, "", retry, {}));
        QVERIFY(model.addConnection(0, "replacement"));
        model.fetchMore(model.index(0, 0));
        QTRY_COMPARE(requested.count(), 3);
        QVERIFY(!model.applyChildren(0, "", retry, {}));
        QVERIFY(model.applyChildren(0, "", requested.last().at(2).toULongLong(), {}));
        QVERIFY(!model.hasChildren(model.index(0, 0)));
    }
    void duplicateIdentifiersFailWithoutAmbiguousChildren() {
        NavigatorModel model;
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(1, "test"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(!model.applyChildren(
            1, "", requested.last().at(2).toULongLong(),
            {{"a", "A", "A", "table", false}, {"a", "B", "B", "table", false}}));
        QVERIFY(!model.canFetchMore(root));
        QCOMPARE(model.rowCount(root), 1);
        QCOMPARE(model.data(model.index(0, 0, root), NavigatorModel::KindRole).toString(),
                 QString("error"));
    }
    void lazilyLoadsOnlyRequestedNodes() {
        NavigatorModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        tester.setUseFetchMore(false);
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(0, "SQLite"));
        QCOMPARE(model.rowCount(), 1);
        const auto root = model.index(0, 0);
        QVERIFY(model.hasChildren(root));
        QCOMPARE(model.rowCount(root), 0);
        QCOMPARE(requested.count(), 0);
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QCOMPARE(model.rowCount(root), 1);
        QCOMPARE(model.data(model.index(0, 0, root), NavigatorModel::KindRole).toString(),
                 QString("loading"));
        const auto token = requested.at(0).at(2).toULongLong();
        QVERIFY(model.applyChildren(0, "", token, {{"table:t", "t", "\"t\"", "table", true}}));
        QCOMPARE(model.rowCount(root), 1);
        const auto table = model.index(0, 0, root);
        QCOMPARE(model.parent(table), root);
        QVERIFY(model.canFetchMore(table));
        QCOMPARE(model.rowCount(table), 0);
        QCOMPARE(requested.count(), 1);
    }
};
QTEST_MAIN(NavigatorModelTest)
#include "navigator_model_test.moc"
