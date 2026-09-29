#include "app/navigator_controller.h"
#include "bridge/engine_adapter.h"
#include "models/navigator_model.h"
#include <QLineEdit>
#include <QTreeView>
#include <QtTest>
using namespace choscordb;

class NavigatorPinResolveTest : public QObject {
    Q_OBJECT
  private slots:
    void hiddenConnectionResolvesPagedObjectWithoutNavigatingExplorer() {
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
        QVERIFY(model->addConnection(41, "Saved"));
        filter.setText("blocked");

        QList<NavigatorController::RevealResult> results;
        QModelIndex resolved;
        QVERIFY(controller.resolveObject(41, {"schema"}, "target", "table", "main.target", {},
                                         [&](NavigatorController::RevealResult result,
                                             const QString&, const QModelIndex& index) {
                                             results.append(result);
                                             resolved = index;
                                         }));
        QTRY_COMPARE(first.count(), 1);
        QVERIFY(model->applyChildren(41, {}, first.last().at(2).toULongLong(),
                                     {{"schema", "main", "main", "schema", true}}));
        QTRY_COMPARE(first.count(), 2);
        QVERIFY(model->applyChildrenPage(41, "schema", first.last().at(2).toULongLong(),
                                         {{"other", "other", "main.other", "table", false}}, 0,
                                         true, 1));
        QTRY_COMPARE(next.count(), 1);
        QVERIFY(model->applyChildrenPage(41, "schema", next.last().at(2).toULongLong(),
                                         {{"target", "target", "main.target", "table", true}}, 1,
                                         false, 2));
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results.first(), NavigatorController::RevealResult::Found);
        QCOMPARE(resolved.model(), static_cast<const QAbstractItemModel*>(model));
        QCOMPARE(resolved.data(NavigatorModel::ObjectIdRole).toString(), QString("target"));
        QCOMPARE(resolved.parent().data(NavigatorModel::ObjectIdRole).toString(),
                 QString("schema"));
        QCOMPARE(filter.text(), QString("blocked"));
        QVERIFY(!tree.currentIndex().isValid());
    }

    void reusedIdentityIsUnavailableAndNeverReturned() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(42, "Saved"));
        QList<NavigatorController::RevealResult> results;
        QModelIndex resolved;
        QVERIFY(controller.resolveObject(42, {}, "same-id", "table", "main.old", {},
                                         [&](NavigatorController::RevealResult result,
                                             const QString&, const QModelIndex& index) {
                                             results.append(result);
                                             resolved = index;
                                         }));
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(42, {}, requested.last().at(2).toULongLong(),
                                     {{"same-id", "new", "main.new", "table", true}}));
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results.first(), NavigatorController::RevealResult::Unavailable);
        QVERIFY(!resolved.isValid());
        QVERIFY(!tree.currentIndex().isValid());
    }

    void staleAndFailedLookupsRemainRetryable() {
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(43, "Saved"));
        bool current = true;
        QList<NavigatorController::RevealResult> results;
        QVERIFY(controller.resolveObject(
            43, {}, "wanted", "table", "main.wanted", {},
            [&](NavigatorController::RevealResult result, const QString&,
                const QModelIndex& index) {
                results.append(result);
                QVERIFY(!index.isValid());
            },
            [&] { return current; }));
        QTRY_COMPARE(requested.count(), 1);
        current = false;
        QVERIFY(model->applyChildren(43, {}, requested.last().at(2).toULongLong(),
                                     {{"wanted", "wanted", "main.wanted", "table", true}}));
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results.first(), NavigatorController::RevealResult::Retry);

        results.clear();
        QVERIFY(controller.resolveObject(43, {}, "wanted", "table", "main.wanted", {},
                                         [&](NavigatorController::RevealResult result,
                                             const QString&, const QModelIndex& index) {
                                             results.append(result);
                                             QVERIFY(!index.isValid());
                                         }));
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(
            model->failChildren(43, {}, requested.last().at(2).toULongLong(), "catalog timeout"));
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results.first(), NavigatorController::RevealResult::Retry);
    }
};
QTEST_MAIN(NavigatorPinResolveTest)
#include "navigator_pin_resolve_test.moc"
