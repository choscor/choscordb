#include "app/main_window_widgets.h"
#include "app/navigator_controller.h"
#include "bridge/engine_adapter.h"
#include "design_system/metrics/metrics.h"
#include "design_system/theme.h"
#include "design_system/tree/navigation_tree_view.h"
#include "models/navigator_model.h"
#include <QAbstractItemModelTester>
#include <QLineEdit>
#include <QMenu>
#include <QSet>
#include <QTimer>
#include <QTreeView>
#include <QtTest>
using namespace choscordb;
class NavigatorRowStatusTest : public QObject {
    Q_OBJECT
  private slots:
    // Rust tests own the texts; this checks that each C++ cause reaches its own Rust policy.
    void rowStatusCausesMapToRustPolicyInOrder() {
        struct Case {
            RowStatusCause cause;
            RowStatusKind kind;
            RowRetry retry;
        };
        const QList<Case> cases{
            {RowStatusCause::Connecting, RowStatusKind::Loading, RowRetry::None},
            {RowStatusCause::ChildrenLoading, RowStatusKind::Loading, RowRetry::None},
            {RowStatusCause::ChildrenFailed, RowStatusKind::Failed, RowRetry::Refresh},
            {RowStatusCause::ConnectionFailed, RowStatusKind::Failed, RowRetry::Refresh},
            {RowStatusCause::ConnectionUnavailable, RowStatusKind::Failed, RowRetry::Refresh},
            {RowStatusCause::ConnectionNotReady, RowStatusKind::Failed, RowRetry::Refresh},
            {RowStatusCause::ConnectionDeselected, RowStatusKind::Failed, RowRetry::Refresh},
            {RowStatusCause::ObjectUnavailable, RowStatusKind::Unavailable, RowRetry::None},
            {RowStatusCause::Connected, RowStatusKind::None, RowRetry::None},
            {RowStatusCause::Hidden, RowStatusKind::None, RowRetry::None},
            {RowStatusCause::Disconnected, RowStatusKind::None, RowRetry::None},
            {RowStatusCause::ChildrenCached, RowStatusKind::Loading, RowRetry::None},
            {RowStatusCause::ChildrenCachedFailed, RowStatusKind::Failed, RowRetry::Refresh},
        };
        QSet<QString> messages;
        for (const auto& expected : cases) {
            const auto status = EngineAdapter::rowStatus(expected.cause);
            QCOMPARE(status.kind, expected.kind);
            QCOMPARE(status.retry, expected.retry);
            QVERIFY(!status.message.isEmpty());
            messages.insert(status.message);
        }
        QCOMPARE(messages.size(), cases.size());
        const auto detailed =
            EngineAdapter::rowStatus(RowStatusCause::RevealFailed, "Table dropped.");
        QVERIFY(detailed.message.startsWith("Table dropped."));
        QVERIFY(detailed.message.endsWith(EngineAdapter::recoveryHint(RecoveryHint::Refresh)));
    }
    void pendingConnectionShowsOnlyItsNameWithLoadingStatus() {
        NavigatorModel model;
        QVERIFY(model.addPendingConnection(3, "Production"));
        const auto root = model.index(0, 0);
        QCOMPARE(root.data().toString(), QString("Production"));
        QCOMPARE(root.data(NavigatorModel::KindRole).toString(), QString("connection"));
        QCOMPARE(root.data(NavigatorModel::StatusRole).toInt(), int(RowStatusKind::Loading));
        QCOMPARE(root.data(Qt::ToolTipRole).toString(),
                 EngineAdapter::rowStatus(RowStatusCause::Connecting).message);
        QVERIFY(!root.data(Qt::ToolTipRole).toString().isEmpty());
        QVERIFY(model.addConnection(4, "Staging"));
        QCOMPARE(model.index(1, 0).data(NavigatorModel::StatusRole).toInt(),
                 int(RowStatusKind::None));
        QVERIFY(model.index(1, 0).data(Qt::ToolTipRole).isNull());
    }
    void navigatorFilterMatchesNamesButNotStatusText() {
        EngineAdapter adapter;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController navigator(&adapter, &tree, &filter);
        auto* model = navigator.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &adapter,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        navigator.addConnection(9, "Warehouse");
        navigator.setVisibleConnections({9});
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(9, {}, requested.last().at(2).toULongLong(),
                                     {{"orders", "orders", "public.orders", "table", true}}));
        const auto orders = model->index(0, 0, root);
        model->fetchMore(orders);
        QCOMPARE(orders.data(NavigatorModel::StatusRole).toInt(), int(RowStatusKind::Loading));
        QVERIFY(orders.data(Qt::ToolTipRole).toString().contains("Loading"));
        const auto visibleRoot = [&tree] { return tree.model()->index(0, 0); };
        filter.setText("Loading");
        QTRY_COMPARE(tree.model()->rowCount(visibleRoot()), 0);
        filter.setText("orde");
        QTRY_COMPARE(tree.model()->rowCount(visibleRoot()), 1);
        QCOMPARE(tree.model()->index(0, 0, visibleRoot()).data().toString(), QString("orders"));
    }
    void failedChildrenMarkTheParentUntilARetry() {
        NavigatorModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        tester.setUseFetchMore(false);
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(7, "Disconnected database"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QCOMPARE(model.rowCount(root), 0);
        QVERIFY(model.hasChildren(root));
        QCOMPARE(root.data(NavigatorModel::StatusRole).toInt(), int(RowStatusKind::Loading));
        QCOMPARE(root.data(Qt::ToolTipRole).toString(), QString("Loading children…"));
        QTRY_COMPARE(requested.count(), 1);
        const auto failedToken = requested.last().at(2).toULongLong();
        QVERIFY(model.failChildren(7, {}, failedToken, "Connection is disconnected"));
        QCOMPARE(model.rowCount(root), 0);
        QVERIFY(model.hasChildren(root));
        QCOMPARE(root.data().toString(), QString("Disconnected database"));
        QCOMPARE(root.data(NavigatorModel::StatusRole).toInt(), int(RowStatusKind::Failed));
        QCOMPARE(root.data(NavigatorModel::RetryRole).toInt(), int(RowRetry::Refresh));
        const QString message(
            "Could not load children: Connection is disconnected. Refresh to retry.");
        QCOMPARE(root.data(Qt::ToolTipRole).toString(), message);
        QCOMPARE(root.data(Qt::AccessibleDescriptionRole).toString(), message);
        QCOMPARE(root.data(NavigatorModel::ErrorRole).toString(),
                 QString("Connection is disconnected"));
        // A view that probes again does not retry by itself.
        QVERIFY(!model.canFetchMore(root));
        model.fetchMore(root);
        bool nextTurn = false;
        QTimer::singleShot(0, &model, [&nextTurn] { nextTurn = true; });
        QTRY_VERIFY(nextTurn);
        QCOMPARE(requested.count(), 1);
        // Refresh retries. NavigatorModelTest covers the retry when the user expands the row.
        model.refresh(root);
        QTRY_COMPARE(requested.count(), 2);
        const auto retryToken = requested.last().at(2).toULongLong();
        QVERIFY(retryToken > failedToken);
        QVERIFY(!model.failChildren(7, {}, failedToken, "Late error"));
        QVERIFY(!model.applyChildren(7, {}, failedToken, {}));
        QVERIFY(model.applyChildren(
            7, {}, retryToken, {{"customers", "Customers", "public.customers", "table", false}}));
        QCOMPARE(model.rowCount(root), 1);
        QCOMPARE(model.index(0, 0, root).data().toString(), QString("Customers"));
        QCOMPARE(root.data(NavigatorModel::StatusRole).toInt(), int(RowStatusKind::None));
        QVERIFY(root.data(Qt::ToolTipRole).isNull());
        QVERIFY(root.data(NavigatorModel::ErrorRole).toString().isEmpty());
        QVERIFY(root.data(NavigatorModel::ChildrenLoadedRole).toBool());
    }
    void navigatorDelegatePaintsStatusIconsAndAFailedRowRed() {
        NavigatorModel model;
        QVERIFY(model.addConnection(7, "Warehouse"));
        QVERIFY(model.addConnection(8, "Archive"));
        QVERIFY(model.addConnection(9, "Reports"));
        design::NavigationTreeView tree;
        tree.setModel(&model);
        tree.setItemDelegate(new main_window_detail::NavigatorIconDelegate(&tree));
        tree.setHeaderHidden(true);
        tree.resize(260, 160);
        tree.show();
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        const auto failed = model.index(0, 0);
        model.fetchMore(failed);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model.failChildren(7, {}, requested.last().at(2).toULongLong(), "offline"));
        const auto loading = model.index(2, 0);
        model.fetchMore(loading);
        QCOMPARE(loading.data(NavigatorModel::StatusRole).toInt(), int(RowStatusKind::Loading));
        tree.setCurrentIndex(model.index(1, 0));
        QCoreApplication::processEvents();
        const auto colors = design::resolvedThemeForWidget(tree).colors;
        const auto image = tree.viewport()->grab().toImage();
        const auto scale = image.devicePixelRatio();
        const auto at = [&](const QRect& row) {
            return image.pixelColor(qRound(row.center().x() * scale),
                                    qRound((row.top() + 2) * scale));
        };
        // Antialiased strokes rarely hit the exact token, so ink is a near match.
        const auto near = [](const QColor& a, const QColor& b) {
            return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) +
                       qAbs(a.blue() - b.blue()) <
                   120;
        };
        // Counts the pixels of one color in the trailing icon square of a row.
        const auto iconInk = [&](const QRect& row, const QColor& color) {
            const int size = design::dimension(design::Dimension::IconSmall);
            int count = 0;
            for (int y = row.center().y() - size / 2; y <= row.center().y() + size / 2; ++y)
                for (int x = row.right() - size - design::spacing(design::Spacing::Two);
                     x <= row.right(); ++x)
                    count += near(image.pixelColor(qRound(x * scale), qRound(y * scale)), color);
            return count;
        };
        QCOMPARE(at(tree.visualRect(failed)), colors.dangerSurface);
        QVERIFY(at(tree.visualRect(model.index(1, 0))) != colors.dangerSurface);
        QVERIFY(iconInk(tree.visualRect(failed), colors.danger) > 0);
        QVERIFY(iconInk(tree.visualRect(loading), colors.fgMuted) > 0);
        QCOMPARE(iconInk(tree.visualRect(loading), colors.danger), 0);
        QCOMPARE(iconInk(tree.visualRect(model.index(1, 0)), colors.fgMuted), 0);
        QCOMPARE(failed.data().toString(), QString("Warehouse"));
    }
    void savedChildrenShowAsLoadingUntilLiveChildrenReplaceThem() {
        NavigatorModel model;
        QAbstractItemModelTester tester(&model);
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(7, "db"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QCOMPARE(requested.last().at(3).toBool(), false);
        auto token = requested.takeLast().at(2).toULongLong();
        QVERIFY(model.applyCachedChildren(7, {}, token, {{"old", "old", "old", "schema", true}}));
        QCOMPARE(model.rowCount(root), 1);
        QCOMPARE(model.index(0, 0, root).data().toString(), QString("old"));
        QCOMPARE(root.data(NavigatorModel::StatusRole).toInt(), int(RowStatusKind::Loading));
        QCOMPARE(root.data(Qt::ToolTipRole).toString(),
                 EngineAdapter::rowStatus(RowStatusCause::ChildrenCached).message);
        QVERIFY(!root.data(NavigatorModel::ChildrenLoadedRole).toBool());
        QCOMPARE(model.pendingRequestToken(root), token);
        // The live children replace the saved children and clear the saved state.
        QVERIFY(model.applyChildren(
            7, {}, token,
            {{"old", "old", "old", "schema", true}, {"new", "new", "new", "schema", true}}));
        QCOMPARE(model.rowCount(root), 2);
        QCOMPARE(model.index(1, 0, root).data().toString(), QString("new"));
        QCOMPARE(root.data(NavigatorModel::StatusRole).toInt(), int(RowStatusKind::None));
        QVERIFY(root.data(NavigatorModel::ChildrenLoadedRole).toBool());

        // A manual refresh asks for live children only.
        model.refresh(root);
        QTRY_COMPARE(requested.count(), 1);
        QCOMPARE(requested.last().at(3).toBool(), true);
        token = requested.takeLast().at(2).toULongLong();
        QVERIFY(model.applyChildren(7, {}, token, {{"new", "new", "new", "schema", true}}));
    }

    void liveChildrenKeepTheSavedNodesThatTheyStillList() {
        NavigatorModel model;
        QAbstractItemModelTester tester(&model);
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(7, "db"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        const auto token = requested.takeLast().at(2).toULongLong();
        QVERIFY(model.applyCachedChildren(
            7, {}, token,
            {{"gone", "gone", "gone", "schema", true}, {"kept", "kept", "kept", "schema", true}}));
        // The user expands a saved child before the live page arrives.
        const QPersistentModelIndex kept = model.index(1, 0, root);
        model.fetchMore(kept);
        QTRY_VERIFY(model.pendingRequestToken(kept) != 0);
        const auto childToken = model.pendingRequestToken(kept);
        QVERIFY(model.applyChildren(7, {}, token,
                                    {{"first", "first", "first", "schema", true},
                                     {"kept", "Kept", "kept", "schema", true},
                                     {"last", "last", "last", "schema", true}}));
        QVERIFY(kept.isValid());
        QCOMPARE(kept.row(), 1);
        QCOMPARE(kept.data().toString(), QString("Kept"));
        QCOMPARE(model.pendingRequestToken(kept), childToken);
        QCOMPARE(model.rowCount(root), 3);
        QCOMPARE(model.index(0, 0, root).data().toString(), QString("first"));
        QCOMPARE(model.index(2, 0, root).data().toString(), QString("last"));
        QVERIFY(model.applyChildren(7, "kept", childToken, {{"t", "t", "t", "table", false}}));
        QCOMPARE(model.rowCount(kept), 1);
    }

    void aKeptSavedSchemaFollowsItsLiveName() {
        NavigatorModel model;
        model.setDriverResolver([](quint64) { return QStringLiteral("postgres"); });
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(7, "db"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        const auto token = requested.last().at(2).toULongLong();
        QVERIFY(model.applyCachedChildren(7, {}, token,
                                          {{"oid-1", "pg_catalog", "pg_catalog", "schema", true}}));
        const QPersistentModelIndex schema = model.index(0, 0, root);
        QVERIFY(!model.isBrowsable(schema));
        // PostgreSQL keeps the ID of a renamed schema.
        QVERIFY(model.applyChildren(7, {}, token, {{"oid-1", "app", "app", "schema", true}}));
        QVERIFY(schema.isValid());
        QVERIFY(model.isBrowsable(schema));
    }

    void aFailedLiveLoadKeepsTheSavedChildrenAndShowsTheError() {
        NavigatorModel model;
        QAbstractItemModelTester tester(&model);
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        QVERIFY(model.addConnection(7, "db"));
        const auto root = model.index(0, 0);
        model.fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        const auto token = requested.last().at(2).toULongLong();
        QVERIFY(model.applyCachedChildren(7, {}, token, {{"old", "old", "old", "schema", true}}));
        QVERIFY(model.failChildren(7, {}, token, "timeout"));
        QCOMPARE(model.rowCount(root), 1);
        QCOMPARE(model.index(0, 0, root).data().toString(), QString("old"));
        QCOMPARE(root.data(NavigatorModel::StatusRole).toInt(), int(RowStatusKind::Failed));
        QCOMPARE(root.data(NavigatorModel::RetryRole).toInt(), int(RowRetry::Refresh));
        QCOMPARE(root.data(Qt::ToolTipRole).toString(),
                 EngineAdapter::rowStatus(RowStatusCause::ChildrenCachedFailed, "timeout").message);
        QVERIFY(!root.data(NavigatorModel::ChildrenLoadedRole).toBool());
        // A late saved page cannot replace the shown state.
        QVERIFY(!model.applyCachedChildren(7, {}, token, {{"x", "x", "x", "schema", true}}));
    }

    void failedRowMenuHasRefreshThatRetries() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(7, "Warehouse"));
        controller.setSelectedConnection(7);
        const auto root = model->index(0, 0);
        QMenu healthy;
        controller.populateContextMenu(&healthy, root);
        QVERIFY(!healthy.findChild<QAction*>("refreshFailedRow"));
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->failChildren(7, {}, requested.last().at(2).toULongLong(), "offline"));
        QMenu menu;
        controller.populateContextMenu(&menu, root);
        auto* refresh = menu.findChild<QAction*>("refreshFailedRow");
        QVERIFY(refresh);
        QCOMPARE(refresh->text(), QString("Refresh"));
        QCOMPARE(menu.actions().first(), refresh);
        refresh->trigger();
        QCOMPARE(root.data(NavigatorModel::StatusRole).toInt(), int(RowStatusKind::Loading));
        QTRY_COMPARE(requested.count(), 2);
    }
};
QTEST_MAIN(NavigatorRowStatusTest)
#include "navigator_row_status_test.moc"
