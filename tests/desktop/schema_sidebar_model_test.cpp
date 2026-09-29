#include "app/navigator_controller.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "models/navigator_model.h"
#include <QLineEdit>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QtTest>
using namespace choscordb;
class SchemaSidebarModelTest final : public QObject {
    Q_OBJECT
  private slots:
    void bridgeMetadataKeepsEachColumnsExactDatabaseTypeWithoutChangingIdentity() {
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
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        BridgeEvent tableReply;
        tableReply.kind = "metadata";
        tableReply.id = 7;
        tableReply.request_token = requested.last().at(2).toULongLong();
        MetadataDto table;
        table.id = "table";
        table.name = "orders";
        table.qualified_name = "public.orders";
        table.kind = "table";
        table.has_children = true;
        tableReply.objects.push_back(std::move(table));
        engine.eventReady(tableReply);
        const auto tableIndex = model->index(0, 0, root);
        model->fetchMore(tableIndex);
        QTRY_COMPARE(requested.count(), 2);

        BridgeEvent columnsReply;
        columnsReply.kind = "metadata";
        columnsReply.id = 7;
        columnsReply.parent = "table";
        columnsReply.request_token = requested.last().at(2).toULongLong();
        const auto column = [](const char* id, const char* name, const char* type,
                               bool hasColumn = true) {
            MetadataDto dto;
            dto.id = id;
            dto.name = name;
            dto.qualified_name = id;
            dto.kind = "column";
            dto.has_column = hasColumn;
            dto.column.database_type = type;
            return dto;
        };
        columnsReply.objects.push_back(
            column("created-id", "created_at", "timestamp(6) with time zone"));
        columnsReply.objects.push_back(column("amount-id", "amount", "numeric(12, 4)"));
        columnsReply.objects.push_back(column("unknown-id", "unknown", "ignored", false));
        engine.eventReady(columnsReply);

        const auto* visible = qobject_cast<QSortFilterProxyModel*>(tree.model());
        QVERIFY(visible);
        const auto shownTable = visible->mapFromSource(tableIndex);
        QCOMPARE(visible->rowCount(shownTable), 3);
        const auto created = visible->index(0, 0, shownTable);
        const auto amount = visible->index(1, 0, shownTable);
        const auto unknown = visible->index(2, 0, shownTable);
        constexpr int databaseTypeRole = NavigatorModel::DatabaseTypeRole;
        QCOMPARE(created.data(databaseTypeRole).toString(), QString("timestamp(6) with time zone"));
        QCOMPARE(amount.data(databaseTypeRole).toString(), QString("numeric(12, 4)"));
        QCOMPARE(unknown.data(databaseTypeRole).toString(), QString());
        QCOMPARE(created.data(Qt::DisplayRole).toString(), QString("created_at"));
        QCOMPARE(created.data(NavigatorModel::ObjectIdRole).toString(), QString("created-id"));
        QCOMPARE(created.data(NavigatorModel::QualifiedNameRole).toString(), QString("created-id"));
        QCOMPARE(created.data(Qt::ToolTipRole).toString(),
                 QString("created_at — timestamp(6) with time zone"));
        QCOMPARE(created.data(Qt::AccessibleDescriptionRole).toString(),
                 QString("created_at — timestamp(6) with time zone"));
        QCOMPARE(unknown.data(Qt::ToolTipRole).toString(), QString("unknown"));
        QCOMPARE(unknown.data(Qt::AccessibleDescriptionRole).toString(), QString("unknown"));
    }

    void tableAndViewExposeOnlyColumnsInSidebarWhileMetadataStaysAvailable() {
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
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(7, {}, requested.last().at(2).toULongLong(),
                                     {{"schema", "public", "public", "schema", true}}));
        const auto schema = model->index(0, 0, root);
        model->fetchMore(schema);
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model->applyChildren(7, "schema", requested.last().at(2).toULongLong(),
                                     {{"table", "orders", "public.orders", "table", true},
                                      {"view", "summary", "public.summary", "view", true},
                                      {"group", "Indexes", {}, "group", true}}));
        const auto table = model->index(0, 0, schema);
        const auto view = model->index(1, 0, schema);
        const auto group = model->index(2, 0, schema);
        model->fetchMore(table);
        QTRY_COMPARE(requested.count(), 3);
        QVERIFY(model->applyChildren(
            7, "table", requested.last().at(2).toULongLong(),
            {{"table-column", "customer_id", "public.orders.customer_id", "column", false},
             {"table-index", "customer_idx", "public.orders.customer_idx", "index", false},
             {"table-key", "customer_pk", "public.orders.customer_pk", "primarykey", false}}));
        model->fetchMore(view);
        QTRY_COMPARE(requested.count(), 4);
        QVERIFY(model->applyChildren(
            7, "view", requested.last().at(2).toULongLong(),
            {{"view-key", "summary_key", "public.summary.summary_key", "uniquekey", false},
             {"view-column", "total", "public.summary.total", "column", false}}));
        model->fetchMore(group);
        QTRY_COMPARE(requested.count(), 5);
        QVERIFY(
            model->applyChildren(7, "group", requested.last().at(2).toULongLong(),
                                 {{"schema-index", "global_idx", "global_idx", "index", false}}));

        auto* visible = qobject_cast<QSortFilterProxyModel*>(tree.model());
        QVERIFY(visible);
        const auto visibleTable = visible->mapFromSource(table);
        const auto visibleView = visible->mapFromSource(view);
        const auto visibleGroup = visible->mapFromSource(group);
        QCOMPARE(visible->rowCount(visibleTable), 1);
        QCOMPARE(visible->index(0, 0, visibleTable).data(NavigatorModel::ObjectIdRole).toString(),
                 QString("table-column"));
        QCOMPARE(visible->rowCount(visibleView), 1);
        QCOMPARE(visible->index(0, 0, visibleView).data(NavigatorModel::ObjectIdRole).toString(),
                 QString("view-column"));
        QCOMPARE(visible->rowCount(visibleGroup), 1);
        QCOMPARE(visible->index(0, 0, visibleGroup).data(NavigatorModel::ObjectIdRole).toString(),
                 QString("schema-index"));
        QCOMPARE(model->rowCount(table), 3);
        QVERIFY(
            model->matchesObject(7, "table-index", "index", "public.orders.customer_idx", "table"));
        QVERIFY(model->matchesObject(7, "table-key", "primarykey", "public.orders.customer_pk",
                                     "table"));

        filter.setText("customer_idx");
        QVERIFY(!visible->mapFromSource(table).isValid());
        QVERIFY(!visible->mapFromSource(model->index(1, 0, table)).isValid());
        filter.setText("customer_id");
        QCOMPARE(visible->rowCount(visible->mapFromSource(table)), 1);
        QCOMPARE(visible->index(0, 0, visible->mapFromSource(table))
                     .data(NavigatorModel::ObjectIdRole)
                     .toString(),
                 QString("table-column"));
    }

    void sidebarFilterFindsColumnsInCollapsedTablesWithinSearchBudget() {
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
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(7, {}, requested.last().at(2).toULongLong(),
                                     {{"schema", "public", "public", "schema", true}}));
        const auto schema = model->index(0, 0, root);
        model->fetchMore(schema);
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(model->applyChildren(7, "schema", requested.last().at(2).toULongLong(),
                                     {{"table", "orders", "public.orders", "table", true}}));
        const auto table = model->index(0, 0, schema);
        QVERIFY(model->canFetchMore(table));

        filter.setText("needle_col");
        QTRY_COMPARE(requested.count(), 3);
        QCOMPARE(requested.last().at(1).toString(), QString("table"));
        QVERIFY(model->applyChildren(
            7, "table", requested.last().at(2).toULongLong(),
            {{"hidden-index", "needle_col_idx", "public.orders.needle_col_idx", "index", false},
             {"column", "needle_col", "public.orders.needle_col", "column", false}}));
        auto* visible = qobject_cast<QSortFilterProxyModel*>(tree.model());
        QVERIFY(visible);
        QTRY_VERIFY(visible->mapFromSource(table).isValid());
        const auto shownTable = visible->mapFromSource(table);
        QCOMPARE(visible->rowCount(shownTable), 1);
        QCOMPARE(visible->index(0, 0, shownTable).data(NavigatorModel::ObjectIdRole).toString(),
                 QString("column"));
        QVERIFY(!visible->mapFromSource(model->index(0, 0, table)).isValid());
    }

    void tablePagesRefreshAndFailureNeverRevealHiddenRowsOrStaleTypes() {
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
        controller.addConnection(7, "Selected");
        controller.setSelectedConnection(7);
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(7, {}, requested.last().at(2).toULongLong(),
                                     {{"table", "orders", "orders", "table", true}}));
        const auto table = model->index(0, 0, root);
        auto* visible = qobject_cast<QSortFilterProxyModel*>(tree.model());
        QVERIFY(visible);
        const auto shownTable = visible->mapFromSource(table);
        model->fetchMore(table);
        QTRY_COMPARE(requested.count(), 2);
        QCOMPARE(visible->index(0, 0, shownTable).data(NavigatorModel::KindRole).toString(),
                 QString("loading"));
        QVERIFY(model->applyChildrenPage(7, "table", requested.last().at(2).toULongLong(),
                                         {{"index", "orders_idx", "orders_idx", "index", false}}, 0,
                                         true, 1));
        QCOMPARE(visible->rowCount(shownTable), 1);
        QCOMPARE(visible->index(0, 0, shownTable).data(NavigatorModel::KindRole).toString(),
                 QString("load_more"));
        model->requestNextPage(table);
        QTRY_COMPARE(next.count(), 1);
        QCOMPARE(next.last().at(3).toULongLong(), quint64(1));
        const auto staleToken = next.last().at(2).toULongLong();
        NavigatorObject column{"column", "amount", "orders.amount", "column", false};
        column.databaseType = "decimal(10,2)";
        QVERIFY(model->applyChildrenPage(
            7, "table", staleToken,
            {column, {"key", "orders_pk", "orders_pk", "primarykey", false}}, 1, false, 3));
        QCOMPARE(visible->rowCount(shownTable), 1);
        QCOMPARE(visible->index(0, 0, shownTable).data(NavigatorModel::DatabaseTypeRole).toString(),
                 QString("decimal(10,2)"));

        model->refresh(table);
        QTRY_COMPARE(requested.count(), 3);
        const auto freshToken = requested.last().at(2).toULongLong();
        QVERIFY(!model->applyChildren(7, "table", staleToken,
                                      {{"stale", "old", "orders.old", "column", false}}));
        QVERIFY(model->applyChildren(7, "table", freshToken,
                                     {{"new-index", "only_idx", "only_idx", "index", false}}));
        QCOMPARE(visible->rowCount(shownTable), 0);
        QVERIFY(!visible->hasChildren(shownTable));
        QVERIFY(table.data(NavigatorModel::ChildrenLoadedRole).toBool());
        QCOMPARE(model->rowCount(table), 1);
        model->refresh(table);
        QTRY_COMPARE(requested.count(), 4);
        QVERIFY(model->failChildren(7, "table", requested.last().at(2).toULongLong(),
                                    "temporary metadata error"));
        QCOMPARE(visible->rowCount(shownTable), 1);
        QCOMPARE(visible->index(0, 0, shownTable).data(NavigatorModel::KindRole).toString(),
                 QString("error"));
    }
};
QTEST_MAIN(SchemaSidebarModelTest)
#include "schema_sidebar_model_test.moc"
