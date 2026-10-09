#include "app/pinned_tree_model.h"
#include "models/navigator_model.h"

#include <QAbstractItemModelTester>
#include <QtTest>

using namespace choscordb;

namespace {
PinRecord pin(QString name, QString kind, QString id, QString parent = {}) {
    PinRecord record;
    record.profileId = "saved";
    record.profileName = "Warehouse";
    record.objectId = std::move(id);
    record.name = std::move(name);
    record.qualifiedName = "public." + record.name;
    record.kind = std::move(kind);
    record.parentObjectId = std::move(parent);
    return record;
}
} // namespace

class PinnedTreeModelTest : public QObject {
    Q_OBJECT
  private slots:
    void savedRootsKeepOrderContextAndBranchSemanticsWhileDisconnected() {
        NavigatorModel navigator;
        PinnedTreeModel pins(&navigator);
        const auto column = pin("id", "column", "column-id", "table-id");
        const auto table = pin("orders", "table", "table-id", "schema-id");
        const auto schema = pin("public", "schema", "schema-id");
        pins.setPins({column, table, schema});

        QCOMPARE(pins.rowCount(), 3);
        const auto first = pins.index(0, 0);
        QCOMPARE(first.data(Qt::DisplayRole).toString(), QString("id"));
        QCOMPARE(pins.pinKey(first), PinStore::identityKey(column));
        QVERIFY(pins.isPinnedRoot(first));
        QVERIFY(!pins.hasChildren(first));
        QVERIFY(pins.hasChildren(pins.index(1, 0)));
        QVERIFY(pins.hasChildren(pins.index(2, 0)));
        QVERIFY(!pins.sourceIndex(first).isValid());
        pins.setStatus(PinStore::identityKey(table), "Disconnected",
                       PinnedTreeModel::StatusPlacement::TooltipOnly);
        const auto second = pins.index(1, 0);
        QCOMPARE(second.data(Qt::DisplayRole).toString(), QString("orders"));
        QVERIFY(second.data(Qt::ToolTipRole).toString().contains("Warehouse"));
        QVERIFY(second.data(Qt::ToolTipRole).toString().contains("Disconnected"));
        QVERIFY(second.data(Qt::AccessibleDescriptionRole).toString().contains("orders"));
        pins.setStatus(PinStore::identityKey(table), "Loading children…",
                       PinnedTreeModel::StatusPlacement::Label);
        QCOMPARE(second.data(Qt::DisplayRole).toString(), QString("orders — Loading children…"));
        pins.setStatus(PinStore::identityKey(table), "Metadata failed. Expand to retry.",
                       PinnedTreeModel::StatusPlacement::Label);
        QCOMPARE(second.data(Qt::DisplayRole).toString(),
                 QString("orders — Metadata failed. Expand to retry."));
        auto unavailable = column;
        unavailable.unavailable = true;
        pins.setPins({unavailable, table, schema});
        QCOMPARE(pins.index(0, 0).data(Qt::DisplayRole).toString(), QString("id — Unavailable"));
    }

    void verifiedRootMirrorsLazyNestedAndPagedLiveRows() {
        NavigatorModel navigator;
        PinnedTreeModel pins(&navigator);
        const auto schemaPin = pin("public", "schema", "schema-id");
        pins.setPins({schemaPin});
        QVERIFY(navigator.addConnection(42, "Warehouse"));
        const auto connection = navigator.index(0, 0);
        QSignalSpy requested(&navigator, &NavigatorModel::childrenRequested);
        QSignalSpy pageRequested(&navigator, &NavigatorModel::childrenPageRequested);
        navigator.fetchMore(connection);
        QTRY_COMPARE(requested.size(), 1);
        QVERIFY(
            navigator.applyChildren(42, {}, requested.last().at(2).toULongLong(),
                                    {{"schema-id", "public", "public.public", "schema", true}}));
        const auto schema = navigator.index(0, 0, connection);
        const auto pinnedRoot = pins.index(0, 0);
        QVERIFY(pins.setResolved(PinStore::identityKey(schemaPin), schema));
        QVERIFY(pins.hasChildren(pinnedRoot));
        QCOMPARE(pins.rowCount(pinnedRoot), 0);

        pins.fetchMore(pinnedRoot);
        QTRY_COMPARE(requested.size(), 2);
        QCOMPARE(requested.last().at(1).toString(), QString("schema-id"));
        QVERIFY(navigator.applyChildrenPage(
            42, "schema-id", requested.last().at(2).toULongLong(),
            {{"table-id", "orders", "public.orders", "table", true}}, 0, true, 1));
        QCOMPARE(pins.rowCount(pinnedRoot), 2); // Live table and continuation row.
        const auto pinnedTable = pins.index(0, 0, pinnedRoot);
        QCOMPARE(pins.sourceIndex(pinnedTable), navigator.index(0, 0, schema));
        QVERIFY(!pins.isPinnedRoot(pinnedTable));
        QVERIFY(pins.hasChildren(pinnedTable));
        pins.fetchMore(pinnedTable);
        QTRY_COMPARE(requested.size(), 3);
        QVERIFY(
            navigator.applyChildren(42, "table-id", requested.last().at(2).toULongLong(),
                                    {{"column-id", "id", "public.orders.id", "column", false}}));
        QCOMPARE(pins.index(0, 0, pinnedTable).data(Qt::DisplayRole).toString(), QString("id"));
        QCOMPARE(pins.sourceIndex(pins.index(0, 0, pinnedTable)),
                 navigator.index(0, 0, navigator.index(0, 0, schema)));

        navigator.requestNextPage(pins.sourceIndex(pins.index(1, 0, pinnedRoot)));
        QTRY_COMPARE(pageRequested.size(), 1);
        QVERIFY(navigator.applyChildrenPage(
            42, "schema-id", pageRequested.last().at(2).toULongLong(),
            {{"view-id", "summary", "public.summary", "view", false}}, 1, false, 2));
        QCOMPARE(pins.rowCount(pinnedRoot), 2);
        QCOMPARE(pins.index(1, 0, pinnedRoot).data(Qt::DisplayRole).toString(), QString("summary"));
        QCOMPARE(pins.index(0, 0, pinnedTable).data(Qt::DisplayRole).toString(), QString("id"));
    }

    void mismatchDetachesPreviousLiveRowsAndSourceRemovalLeavesSavedRoot() {
        NavigatorModel navigator;
        PinnedTreeModel pins(&navigator);
        const auto schemaPin = pin("public", "schema", "schema-id");
        pins.setPins({schemaPin});
        QVERIFY(navigator.addConnection(11, "Warehouse"));
        const auto connection = navigator.index(0, 0);
        QSignalSpy requested(&navigator, &NavigatorModel::childrenRequested);
        navigator.fetchMore(connection);
        QTRY_COMPARE(requested.size(), 1);
        QVERIFY(navigator.applyChildren(11, {}, requested.last().at(2).toULongLong(),
                                        {{"schema-id", "public", "public.public", "schema", true},
                                         {"other-id", "other", "public.other", "schema", true}}));
        const auto schema = navigator.index(0, 0, connection);
        const auto other = navigator.index(1, 0, connection);
        const auto key = PinStore::identityKey(schemaPin);
        QVERIFY(pins.setResolved(key, schema));
        const QPersistentModelIndex pinnedRoot(pins.index(0, 0));
        pins.fetchMore(pinnedRoot);
        QTRY_COMPARE(requested.size(), 2);
        QVERIFY(navigator.applyChildren(11, "schema-id", requested.last().at(2).toULongLong(),
                                        {{"table-id", "orders", "public.orders", "table", false}}));
        QCOMPARE(pins.rowCount(pinnedRoot), 1);

        QVERIFY(!pins.setResolved(key, other));
        QCOMPARE(pins.rowCount(pinnedRoot), 0);
        QVERIFY(!pins.sourceIndex(pinnedRoot).isValid());
        QVERIFY(pins.setResolved(key, schema));
        QCOMPARE(pins.rowCount(pinnedRoot), 1);
        QVERIFY(navigator.removeConnection(11));
        QVERIFY(pinnedRoot.isValid());
        QCOMPARE(pins.rowCount(pinnedRoot), 0);
        QVERIFY(!pins.sourceIndex(pinnedRoot).isValid());
        QCOMPARE(pins.rowCount(), 1);
    }

    void overlappingPinnedSubtreesReceiveTheSameLiveInsert() {
        NavigatorModel navigator;
        PinnedTreeModel pins(&navigator);
        QAbstractItemModelTester tester(&pins,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto tablePin = pin("orders", "table", "table-id", "schema-id");
        tablePin.ancestryIds = {"schema-id"};
        tablePin.ancestryNames = {"public"};
        const auto schemaPin = pin("public", "schema", "schema-id");
        pins.setPins({tablePin, schemaPin});
        QVERIFY(navigator.addConnection(12, "Warehouse"));
        const auto connection = navigator.index(0, 0);
        QSignalSpy requested(&navigator, &NavigatorModel::childrenRequested);
        navigator.fetchMore(connection);
        QTRY_COMPARE(requested.size(), 1);
        QVERIFY(
            navigator.applyChildren(12, {}, requested.last().at(2).toULongLong(),
                                    {{"schema-id", "public", "public.public", "schema", true}}));
        const auto schema = navigator.index(0, 0, connection);
        QVERIFY(pins.setResolved(PinStore::identityKey(schemaPin), schema));
        navigator.fetchMore(schema);
        QTRY_COMPARE(requested.size(), 2);
        QVERIFY(navigator.applyChildren(12, "schema-id", requested.last().at(2).toULongLong(),
                                        {{"table-id", "orders", "public.orders", "table", true}}));
        const auto table = navigator.index(0, 0, schema);
        QVERIFY(pins.setResolved(PinStore::identityKey(tablePin), table));
        const auto tableRoot = pins.index(0, 0);
        const auto schemaRoot = pins.index(1, 0);
        pins.fetchMore(tableRoot);
        QTRY_COMPARE(requested.size(), 3);
        QVERIFY(
            navigator.applyChildren(12, "table-id", requested.last().at(2).toULongLong(),
                                    {{"column-id", "id", "public.orders.id", "column", false}}));
        QCOMPARE(pins.index(0, 0, tableRoot).data(Qt::DisplayRole).toString(), QString("id"));
        QCOMPARE(pins.index(0, 0, pins.index(0, 0, schemaRoot)).data(Qt::DisplayRole).toString(),
                 QString("id"));
        navigator.refresh(table);
        QCOMPARE(pins.index(0, 0, tableRoot).data(NavigatorModel::KindRole).toString(),
                 QString("loading"));
        QCOMPARE(pins.index(0, 0, pins.index(0, 0, schemaRoot))
                     .data(NavigatorModel::KindRole)
                     .toString(),
                 QString("loading"));
    }
};

QTEST_MAIN(PinnedTreeModelTest)
#include "pinned_tree_model_test.moc"
