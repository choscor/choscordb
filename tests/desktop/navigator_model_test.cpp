#include "models/navigator_model.h"
#include <QAbstractItemModelTester>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QTreeView>
#include <QtTest>
using namespace choscordb;
class NavigatorModelTest : public QObject {
    Q_OBJECT
  private slots:
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
