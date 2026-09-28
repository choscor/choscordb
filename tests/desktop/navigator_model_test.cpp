#include "app/navigator_controller.h"
#include "bridge/engine_adapter.h"
#include "models/navigator_model.h"
#include <QAbstractItemModelTester>
#include <QAction>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QTreeView>
#include <QVariantMap>
#include <QtTest>
#include <algorithm>
using namespace choscordb;
class NavigatorModelTest : public QObject {
    Q_OBJECT
  private slots:
    void objectSnapshotTracksCurrentIdentityAcrossRefreshAndRemoval() {
        NavigatorModel model;
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(7, "First"));
        QVERIFY(model.addConnection(8, "Second"));
        QVERIFY(!model.objectSnapshot(7, "table").has_value());
        const auto first = model.index(0, 0);
        model.fetchMore(first);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model.applyChildren(7, {}, requested.last().at(2).toULongLong(),
                                    {{"schema", "public", "public", "schema", true}}));
        const auto schema = model.index(0, 0, first);
        QVERIFY(!model.objectSnapshot(7, "table").has_value());
        model.fetchMore(schema);
        QTRY_COMPARE(requested.count(), 2);
        const QVariantList properties{
            QVariantMap{{"name", "Relation subtype"}, {"value", "ordinary_table"}}};
        QVERIFY(model.applyChildren(
            7, "schema", requested.last().at(2).toULongLong(),
            {{"table", "orders", "public.orders", "table", false, properties}}));
        const auto loaded = model.objectSnapshot(7, "table");
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->connection, quint64(7));
        QCOMPARE(loaded->objectId, QString("table"));
        QCOMPARE(loaded->name, QString("orders"));
        QCOMPARE(loaded->qualifiedName, QString("public.orders"));
        QCOMPARE(loaded->kind, QString("table"));
        QCOMPARE(loaded->parentObjectId, QString("schema"));
        QCOMPARE(loaded->properties, properties);
        QVERIFY(!model.objectSnapshot(8, "table").has_value());

        model.refresh(schema);
        QVERIFY(!model.objectSnapshot(7, "table").has_value());
        QTRY_COMPARE(requested.count(), 3);
        QVERIFY(model.applyChildren(7, "schema", requested.last().at(2).toULongLong(),
                                    {{"table", "orders_new", "public.orders_new", "view", false}}));
        const auto replacement = model.objectSnapshot(7, "table");
        QVERIFY(replacement.has_value());
        QCOMPARE(replacement->name, QString("orders_new"));
        QCOMPARE(replacement->kind, QString("view"));
        QVERIFY(model.removeConnection(7));
        QVERIFY(!model.objectSnapshot(7, "table").has_value());
    }

    void objectSnapshotRejectsAmbiguousDuplicateIndexIdentity() {
        NavigatorModel model;
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(7, "db"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model.applyChildren(7, {}, requested.last().at(2).toULongLong(),
                                    {{"table", "orders", "orders", "table", true},
                                     {"group", "Indexes", {}, "group", true}}));
        const auto table = model.index(0, 0, root);
        const auto group = model.index(1, 0, root);
        model.fetchMore(table);
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model.applyChildren(7, "table", requested.last().at(2).toULongLong(),
                                    {{"index-id", "orders_idx", "orders_idx", "index", false}}));
        QVERIFY(model.objectSnapshot(7, "index-id").has_value());
        model.fetchMore(group);
        QTRY_COMPARE(requested.count(), 3);
        QVERIFY(model.applyChildren(7, "group", requested.last().at(2).toULongLong(),
                                    {{"index-id", "orders_idx", "orders_idx", "index", false}}));
        QVERIFY(!model.objectSnapshot(7, "index-id").has_value());
    }

    void quickObjectSearchLoadsCollapsedSchemaWithoutChangingSidebar() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        controller.addConnection(7, "Selected");
        controller.addConnection(8, "Other");
        controller.setSelectedConnection(7);
        {
            const QSignalBlocker blocker(&filter);
            filter.setText("sidebar text");
        }
        const auto previousSelection = tree.currentIndex();

        controller.startQuickObjectSearch("sales orders");
        QTRY_COMPARE(requested.count(), 1);
        QCOMPARE(requested.last().at(0).toULongLong(), quint64(7));
        QVERIFY(model->applyChildren(7, {}, requested.last().at(2).toULongLong(),
                                     {{"schema", "public", "public", "schema", true}}));
        QTRY_COMPARE(requested.count(), 2);
        QCOMPARE(requested.last().at(1).toString(), QString("schema"));
        QVERIFY(model->applyChildren(
            7, "schema", requested.last().at(2).toULongLong(),
            {{"table", "Sales_Orders", "public.Sales_Orders", "table", false}}));
        QTRY_COMPARE(controller.quickObjectResults().size(), 1);
        const auto result = controller.quickObjectResults().first();
        QCOMPARE(result.connection, quint64(7));
        QCOMPARE(result.objectId, QString("table"));
        QCOMPARE(result.kind, QString("table"));
        QCOMPARE(result.parentObjectId, QString("schema"));
        QCOMPARE(result.qualifiedName, QString("public.Sales_Orders"));
        QVERIFY(result.context.contains("public"));
        QCOMPARE(filter.text(), QString("sidebar text"));
        QCOMPARE(tree.currentIndex(), previousSelection);
        QVERIFY(!tree.isExpanded(tree.model()->index(0, 0)));
        QVERIFY(!controller.quickObjectSearchIncomplete());
    }

    void quickObjectSearchRespectsPostgresSystemSchemaVisibility() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        controller.setDriverResolver([](quint64) { return QStringLiteral("postgres"); });
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        controller.addConnection(7, "Postgres");
        controller.setSelectedConnection(7);
        controller.startQuickObjectSearch("catalog_table");
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(7, {}, requested.last().at(2).toULongLong(),
                                     {{"catalog", "pg_catalog", "pg_catalog", "schema", true}}));
        QTRY_COMPARE(controller.quickObjectSearchStatus(), QString());
        QCOMPARE(requested.count(), 1);
        QVERIFY(controller.quickObjectResults().isEmpty());

        controller.setShowSystemSchemas(true);
        QTRY_COMPARE(requested.count(), 2);
        QCOMPARE(requested.last().at(1).toString(), QString("catalog"));
        QVERIFY(model->applyChildren(
            7, "catalog", requested.last().at(2).toULongLong(),
            {{"catalog-table", "catalog_table", "pg_catalog.catalog_table", "table", false}}));
        QTRY_COMPARE(controller.quickObjectResults().size(), 1);
        QVERIFY(model->objectSnapshot(7, "catalog-table").has_value());
        QVERIFY(model->matchesObject(7, "catalog-table", "table", "pg_catalog.catalog_table",
                                     "catalog", {}, true));

        controller.setShowSystemSchemas(false);
        QVERIFY(model->canShowUnverifiedObject(7, "\"public\".\"ordinary\""));
        QVERIFY(model->canShowUnverifiedObject(7, "public.ordinary"));
        QVERIFY(!model->canShowUnverifiedObject(7, "\"pg_catalog\".\"hidden\""));
        QVERIFY(!model->canShowUnverifiedObject(7, "pg_catalog.hidden"));
        QVERIFY(!model->canShowUnverifiedObject(7, "unqualified"));
        QTRY_VERIFY(controller.quickObjectResults().isEmpty());
        QVERIFY(!model->objectSnapshot(7, "catalog-table").has_value());
        QVERIFY(model->matchesObject(7, "catalog-table", "table", "pg_catalog.catalog_table",
                                     "catalog"));
        QVERIFY(!model->matchesObject(7, "catalog-table", "table", "pg_catalog.catalog_table",
                                      "catalog", {}, true));
    }

    void quickObjectSearchInvalidatesOldQueriesAndConnectionResults() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        controller.addConnection(7, "First");
        controller.addConnection(8, "Second");
        controller.setSelectedConnection(7);
        controller.startQuickObjectSearch("old");
        QTRY_COMPARE(requested.count(), 1);
        controller.startQuickObjectSearch("new");
        QVERIFY(model->applyChildren(7, {}, requested.first().at(2).toULongLong(),
                                     {{"old", "old table", "old table", "table", false},
                                      {"new", "new table", "new table", "table", false}}));
        QTRY_COMPARE(controller.quickObjectResults().size(), 1);
        QCOMPARE(controller.quickObjectResults().first().objectId, QString("new"));

        controller.setSelectedConnection(8);
        QCOMPARE(controller.quickObjectResults().size(), 0);
        QCOMPARE(controller.quickObjectSearchStatus(), QString());
        controller.startQuickObjectSearch("second");
        QTRY_COMPARE(requested.count(), 2);
        QCOMPARE(requested.last().at(0).toULongLong(), quint64(8));
        QVERIFY(model->applyChildren(8, {}, requested.last().at(2).toULongLong(),
                                     {{"second", "second table", "second table", "table", false}}));
        QTRY_COMPARE(controller.quickObjectResults().size(), 1);
        QCOMPARE(controller.quickObjectResults().first().connection, quint64(8));
        controller.cancelQuickObjectSearch();
        QCOMPARE(controller.quickObjectResults().size(), 0);
    }

    void quickObjectSearchUsesForgivingOrderedNameMatching() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        controller.addConnection(7, "Selected");
        controller.setSelectedConnection(7);
        controller.startQuickObjectSearch("cma");
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(
            7, {}, requested.last().at(2).toULongLong(),
            {{"customer", "CustomerAccounts", "CustomerAccounts", "table", false},
             {"other", "Orders", "Orders", "table", false}}));
        QTRY_COMPARE(controller.quickObjectResults().size(), 1);
        QCOMPARE(controller.quickObjectResults().first().objectId, QString("customer"));
    }

    void quickObjectSearchRetainsValidRowsWhenOneSchemaFails() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        controller.addConnection(7, "Selected");
        controller.setSelectedConnection(7);
        controller.startQuickObjectSearch("matched");
        QTRY_COMPARE(requested.count(), 1);
        const auto root = model->index(0, 0);
        QVERIFY(model->applyChildren(7, {}, requested.last().at(2).toULongLong(),
                                     {{"public", "public", "public", "schema", true},
                                      {"archive", "archive", "archive", "schema", true}}));
        QTRY_COMPARE(requested.count(), 2);
        QCOMPARE(requested.last().at(1).toString(), QString("public"));
        QVERIFY(model->applyChildren(
            7, "public", requested.last().at(2).toULongLong(),
            {{"table", "matched_table", "public.matched_table", "table", false}}));
        QTRY_COMPARE(requested.count(), 3);
        QCOMPARE(requested.last().at(1).toString(), QString("archive"));
        QVERIFY(model->failChildren(7, "archive", requested.last().at(2).toULongLong(),
                                    "Metadata unavailable"));
        QTRY_COMPARE(controller.quickObjectResults().size(), 1);
        QCOMPARE(controller.quickObjectResults().first().objectId, QString("table"));
        QVERIFY(controller.quickObjectSearchIncomplete());
        QVERIFY(controller.quickObjectSearchStatus().contains("Metadata unavailable"));

        model->refresh(root);
        QCOMPARE(controller.quickObjectResults().size(), 0);
    }

    void quickObjectSearchProvidesColumnAndKeyActivationTargets() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        controller.addConnection(7, "Selected");
        controller.setSelectedConnection(7);
        controller.startQuickObjectSearch("customer");
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(7, {}, requested.last().at(2).toULongLong(),
                                     {{"schema", "public", "public", "schema", true}}));
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model->applyChildren(7, "schema", requested.last().at(2).toULongLong(),
                                     {{"table", "orders", "public.orders", "table", true}}));
        QTRY_COMPARE(requested.count(), 3);
        QVERIFY(model->applyChildren(
            7, "table", requested.last().at(2).toULongLong(),
            {{"column", "customer_id", "public.orders.customer_id", "column", false},
             {"key", "customer_pk", "public.orders.customer_pk", "primary_key", false}}));
        QTRY_COMPARE(controller.quickObjectResults().size(), 2);
        const auto results = controller.quickObjectResults();
        QCOMPARE(results.at(0).objectId, QString("column"));
        QCOMPARE(results.at(0).targetObjectId, QString("table"));
        QCOMPARE(results.at(0).targetQualifiedName, QString("public.orders"));
        QCOMPARE(results.at(0).targetKind, QString("table"));
        QCOMPARE(results.at(0).targetParentObjectId, QString("schema"));
        QCOMPARE(results.at(0).targetPane, 0);
        QCOMPARE(results.at(1).objectId, QString("key"));
        QCOMPARE(results.at(1).targetObjectId, QString("table"));
        QCOMPARE(results.at(1).targetPane, 2);

        controller.startQuickObjectSearch("orders");
        QTRY_VERIFY(!controller.quickObjectResults().isEmpty());
        const auto tableResults = controller.quickObjectResults();
        const auto tableResult = std::find_if(
            tableResults.cbegin(), tableResults.cend(),
            [](const QuickObjectResult& result) { return result.kind == QStringLiteral("table"); });
        QVERIFY(tableResult != tableResults.cend());
        QCOMPARE(tableResult->targetObjectId, QString("table"));
        QCOMPARE(tableResult->targetPane, 5);
    }

    void quickObjectSearchSkipsStructuralRowsButTraversesThem() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        controller.addConnection(7, "Selected");
        controller.setSelectedConnection(7);
        controller.startQuickObjectSearch("matched");
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(
            model->applyChildren(7, {}, requested.last().at(2).toULongLong(),
                                 {{"schema", "matched schema", "matched schema", "schema", true}}));
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model->applyChildren(7, "schema", requested.last().at(2).toULongLong(),
                                     {{"group", "matched group", {}, "group", true}}));
        QTRY_COMPARE(requested.count(), 3);
        QVERIFY(
            model->applyChildren(7, "group", requested.last().at(2).toULongLong(),
                                 {{"table", "matched table", "matched table", "table", false}}));
        QTRY_COMPARE(controller.quickObjectResults().size(), 1);
        QCOMPARE(controller.quickObjectResults().first().objectId, QString("table"));
    }

    void quickObjectSearchCanChooseAnyVisibleConnectionIncludingZero() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        controller.addConnection(11, "First");
        controller.addConnection(22, "Second");
        controller.addConnection(0, "Zero");
        controller.setVisibleConnections({11, 22, 0});
        QVERIFY(controller.isVisibleConnection(11));
        QVERIFY(controller.isVisibleConnection(22));
        QVERIFY(controller.isVisibleConnection(0));
        QVERIFY(!controller.isVisibleConnection(99));
        controller.startQuickObjectSearch("needle", 22);
        QTRY_COMPARE(requested.count(), 1);
        QCOMPARE(requested.last().at(0).toULongLong(), quint64(22));
        QVERIFY(
            model->applyChildren(22, {}, requested.last().at(2).toULongLong(),
                                 {{"second", "needle second", "needle second", "table", false}}));
        QTRY_COMPARE(controller.quickObjectResults().size(), 1);
        QCOMPARE(controller.quickObjectResults().first().connection, quint64(22));
        controller.startQuickObjectSearch("needle", quint64(0));
        QTRY_COMPARE(requested.count(), 2);
        QCOMPARE(requested.last().at(0).toULongLong(), quint64(0));
        QVERIFY(model->applyChildren(0, {}, requested.last().at(2).toULongLong(),
                                     {{"zero", "needle zero", "needle zero", "table", false}}));
        QTRY_COMPARE(controller.quickObjectResults().size(), 1);
        QCOMPARE(controller.quickObjectResults().first().connection, quint64(0));
        QVERIFY(!model->index(0, 0).data(NavigatorModel::ChildrenLoadedRole).toBool());
        controller.startQuickObjectSearch("needle", 99);
        QCoreApplication::processEvents();
        QCOMPARE(requested.count(), 2);
        QCOMPARE(controller.quickObjectResults().size(), 0);
        QVERIFY(controller.quickObjectSearchStatus().contains("available"));
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
        QVERIFY(model.matchesObject(1, "index-id", "index", "idx", "table"));
        QVERIFY(model.matchesObject(1, "index-id", "index", "idx", "group"));
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
