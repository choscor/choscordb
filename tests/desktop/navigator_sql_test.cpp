#include "app/navigator_controller.h"
#include "bridge/engine_adapter.h"
#include "bridge/template_service.h"
#include "models/navigator_model.h"
#include <QAction>
#include <QLineEdit>
#include <QMenu>
#include <QTreeView>
#include <QtTest>
class NavigatorSqlTest : public QObject {
    Q_OBJECT
  private slots:
    void disconnectTargetsTheNodeAndRejectsAStaleMenu() {
        choscordb::EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        choscordb::NavigatorController controller(&engine, &tree, &filter, &tree);
        controller.addConnection(9, "First");
        controller.addConnection(10, "Second");
        QSignalSpy requested(&controller, &choscordb::NavigatorController::disconnectRequested);
        QMenu menu;
        controller.populateContextMenu(&menu, controller.model()->index(1, 0));
        auto* disconnect = menu.findChild<QAction*>("disconnectSession");
        QVERIFY(disconnect);
        disconnect->trigger();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.first().at(0).toULongLong(), quint64(10));
        controller.model()->removeConnection(10);
        controller.addConnection(10, "Replacement");
        disconnect->trigger();
        QCOMPARE(requested.count(), 1);
    }
    void templatesPreserveQuotedUnicodeAndRejectInvalidInput() {
        const auto select = choscordb::SqlTemplateService::generate(
            "select", QString::fromUtf8("\"模式.a\".\"Ta\"\"ble\""));
        QVERIFY(select.valid);
        QCOMPARE(select.sql, QString::fromUtf8("SELECT * FROM \"模式.a\".\"Ta\"\"ble\";"));
        QVERIFY(!choscordb::SqlTemplateService::generate("select", QString(QChar(0xd800))).valid);
        QVERIFY(!choscordb::SqlTemplateService::generate("select", "table; DROP TABLE t").valid);
        const auto insert = choscordb::SqlTemplateService::generate("insert", "\"t\"", {"a.b"});
        QVERIFY(insert.valid);
        QVERIFY(insert.sql.startsWith("-- Replace numbered placeholders"));
        QVERIFY(insert.sql.contains("(\"a.b\") VALUES ($1);"));
        const auto update = choscordb::SqlTemplateService::generate("update", "\"t\"", {"a.b"});
        QVERIFY(update.valid);
        QVERIFY(update.sql.contains("WHERE /* predicate */;"));
        const auto remove = choscordb::SqlTemplateService::generate("delete", "\"t\"");
        QVERIFY(remove.valid);
        QCOMPARE(remove.sql, QString("DELETE FROM \"t\" WHERE /* predicate */;"));
        const auto defaults = choscordb::SqlTemplateService::generate("insert", "\"t\"");
        QVERIFY(defaults.valid);
        QCOMPARE(defaults.sql, QString("INSERT INTO \"t\" DEFAULT VALUES;"));
        const auto limits = choscordb::SqlTemplateService::limits();
        QVERIFY(!choscordb::SqlTemplateService::generate(
                     "select", QString(qsizetype(limits.maxBytes / 3 + 1), QChar(0x4e00)))
                     .valid);
        QVERIFY(
            !choscordb::SqlTemplateService::generate("insert", "\"t\"", {QString(QChar(0xd800))})
                 .valid);
    }
    void menuUsesLoadedColumnsAndRejectsRefreshAndRemoval() {
        choscordb::EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        choscordb::NavigatorController controller(&engine, &tree, &filter, &tree);
        auto* model = controller.model();
        // Accepted model fixtures isolate menu behavior from database I/O.
        QObject::disconnect(model, &choscordb::NavigatorModel::childrenRequested, &engine,
                            &choscordb::EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &choscordb::NavigatorModel::childrenRequested);
        QSignalSpy generated(&controller, &choscordb::NavigatorController::sqlGenerated);
        QSignalSpy failures(&controller, &choscordb::NavigatorController::generationFailed);
        controller.addConnection(9, "Fixture");
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(model->applyChildren(9, "", requested.last().at(2).toULongLong(),
                                     {{"table", "table", "\"main\".\"table\"", "table", true}}));
        const auto table = model->index(0, 0, root);
        QMenu unloaded;
        controller.populateContextMenu(&unloaded, table);
        QVERIFY(unloaded.findChild<QAction*>("generate_select"));
        QVERIFY(!unloaded.findChild<QAction*>("generate_insert")->isEnabled());
        unloaded.findChild<QAction*>("generate_select")->trigger();
        QCOMPARE(generated.count(), 1);
        model->fetchMore(table);
        QTRY_COMPARE(requested.count(), 2);
        QVERIFY(
            model->applyChildren(9, "table", requested.last().at(2).toULongLong(),
                                 {{"col", "a.b", "\"main\".\"table\".\"a.b\"", "column", false}}));
        QMenu loaded;
        controller.populateContextMenu(&loaded, table);
        QVERIFY(loaded.findChild<QAction*>("generate_insert")->isEnabled());
        loaded.findChild<QAction*>("generate_insert")->trigger();
        QCOMPARE(generated.count(), 2);
        QVERIFY(generated.last().at(1).toString().contains("\"a.b\""));
        QCOMPARE(requested.count(), 2); // Generation never fetches metadata.
        model->refresh(table);
        loaded.findChild<QAction*>("generate_insert")->trigger();
        QCOMPARE(generated.count(), 2);
        QCOMPARE(failures.count(), 1);
        model->removeConnection(9);
        loaded.findChild<QAction*>("generate_select")->trigger();
        QCOMPARE(generated.count(), 2);
        QCOMPARE(failures.count(), 2);
    }
};
QTEST_MAIN(NavigatorSqlTest)
#include "navigator_sql_test.moc"
