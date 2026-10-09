#include "app/navigator_controller.h"
#include "bridge/engine_adapter.h"
#include "bridge/template_service.h"
#include "models/navigator_model.h"
#include <QAction>
#include <QLineEdit>
#include <QMenu>
#include <QPersistentModelIndex>
#include <QTreeView>
#include <QtTest>
class NavigatorSqlTest : public QObject {
    Q_OBJECT
  private slots:
    void unsupportedRelationSubtypesExplainWhyActionsAreUnavailable() {
        using namespace choscordb;
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter);
        controller.setDriverResolver([](quint64) { return QStringLiteral("mysql"); });
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requested(model, &NavigatorModel::childrenRequested);
        QVERIFY(model->addConnection(10, "fixture"));
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requested.count(), 1);
        const QVariantList subtype{
            QVariantMap{{"name", "Relation subtype"}, {"value", "foreign_table"}}};
        QVERIFY(model->applyChildren(
            10, {}, requested.last().at(2).toULongLong(),
            {{"foreign", "remote_table", "\"remote_table\"", "table", false, subtype}}));
        QSignalSpy actions(&controller, &NavigatorController::objectActionRequested);
        QMenu menu;
        controller.populateContextMenu(&menu, model->index(0, 0, root));
        auto* drop = menu.findChild<QAction*>("dropObject");
        auto* rename = menu.findChild<QAction*>("renameObject");
        QVERIFY(drop && !drop->isEnabled());
        QVERIFY(rename && !rename->isEnabled());
        QCOMPARE(drop->toolTip(), QString("The selected relation type is not supported."));
        QCOMPARE(rename->toolTip(), QString("The selected relation type is not supported."));
        drop->trigger();
        rename->trigger();
        QCOMPARE(actions.count(), 0);
    }

    void postgresRelationSubtypesKeepActionsAndReachTheRequest() {
        using namespace choscordb;
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter);
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

    void postgresSystemSchemasStayOutOfTreeAndCompletionUntilEnabled() {
        using namespace choscordb;
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter);
        controller.setDriverResolver([](quint64 id) {
            return id == 9 ? "postgres" : id == 10 ? "mysql" : "sqlite";
        });
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requests(model, &NavigatorModel::childrenRequested);
        controller.addConnection(9, "PostgreSQL");
        controller.addConnection(10, "MySQL");
        controller.addConnection(11, "SQLite");
        controller.setVisibleConnections({9, 10, 11});
        for (auto connection : {quint64(9), quint64(10), quint64(11)}) {
            const auto root = model->index(int(connection - 9), 0);
            model->fetchMore(root);
            QTRY_COMPARE(requests.count(), int((connection - 9) * 2 + 1));
            const auto databaseId = QString::number(connection) + "-database";
            QVERIFY(model->applyChildren(connection, {}, requests.last().at(2).toULongLong(),
                                         {{databaseId, "database", "database", "database", true}}));
            const auto database = model->index(0, 0, root);
            model->fetchMore(database);
            QTRY_COMPARE(requests.count(), int((connection - 9) * 2 + 2));
            QVERIFY(model->applyChildren(
                connection, databaseId, requests.last().at(2).toULongLong(),
                {{QString::number(connection) + "-catalog", "pg_catalog", "pg_catalog", "schema",
                  true},
                 {QString::number(connection) + "-info", "information_schema", "information_schema",
                  "schema", true},
                 {QString::number(connection) + "-temp", "pg_temp_3", "pg_temp_3", "schema", true},
                 {QString::number(connection) + "-toast-root", "pg_toast", "pg_toast", "schema",
                  true},
                 {QString::number(connection) + "-toast", "pg_toast_temp_3", "pg_toast_temp_3",
                  "schema", true},
                 {QString::number(connection) + "-future", "pg_future_internal",
                  "pg_future_internal", "schema", true},
                 {QString::number(connection) + "-app", "app_tmp", "app_tmp", "schema", true},
                 {QString::number(connection) + "-public", "public", "public", "schema", true}}));
        }
        auto* visible = tree.model();
        const QPersistentModelIndex pgDatabase(visible->index(0, 0, visible->index(0, 0)));
        const QPersistentModelIndex mysqlDatabase(visible->index(0, 0, visible->index(1, 0)));
        const QPersistentModelIndex sqliteDatabase(visible->index(0, 0, visible->index(2, 0)));
        QCOMPARE(visible->rowCount(pgDatabase), 2);
        QCOMPARE(visible->index(0, 0, pgDatabase).data().toString(), QString("app_tmp"));
        QCOMPARE(visible->index(1, 0, pgDatabase).data().toString(), QString("public"));
        QCOMPARE(visible->rowCount(mysqlDatabase), 8);
        QCOMPARE(visible->rowCount(sqliteDatabase), 8);
        auto pgSnapshot = model->completionSnapshot(9, {100, 4096, 100 * 8 + 64});
        QCOMPARE(pgSnapshot.objects.size(), size_t(3));
        QCOMPARE(model->completionSnapshot(10, {100, 4096, 100 * 8 + 64}).objects.size(),
                 size_t(9));
        QCOMPARE(model->completionSnapshot(11, {100, 4096, 100 * 8 + 64}).objects.size(),
                 size_t(9));
        controller.setShowSystemSchemas(true);
        QCOMPARE(visible->rowCount(pgDatabase), 8);
        QCOMPARE(model->completionSnapshot(9, {100, 4096, 100 * 8 + 64}).objects.size(), size_t(9));
        const auto pgRoot = model->index(0, 0);
        const auto pgSourceDatabase = model->index(0, 0, pgRoot);
        const auto catalog = model->index(0, 0, pgSourceDatabase);
        model->fetchMore(catalog);
        QTRY_COMPARE(requests.count(), 7);
        QVERIFY(model->applyChildren(
            9, "9-catalog", requests.last().at(2).toULongLong(),
            {{"catalog-table", "tmp_result", "pg_catalog.tmp_result", "table", false}}));
        QCOMPARE(model->completionSnapshot(9, {100, 4096, 100 * 8 + 64}).objects.size(),
                 size_t(10));
        tree.setCurrentIndex(visible->index(0, 0, pgDatabase));
        controller.setShowSystemSchemas(false);
        QCOMPARE(visible->rowCount(pgDatabase), 2);
        QCOMPARE(tree.currentIndex().data(NavigatorModel::KindRole).toString(),
                 QString("database"));
        QCOMPARE(model->completionSnapshot(9, {100, 4096, 100 * 8 + 64}).objects.size(), size_t(3));
        controller.setShowSystemSchemas(true);
        QCOMPARE(model->completionSnapshot(9, {100, 4096, 100 * 8 + 64}).objects.size(),
                 size_t(10));
    }
    void searchDoesNotFetchHiddenSchemasOrResurrectLateReplies() {
        using namespace choscordb;
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter);
        controller.setDriverResolver([](quint64) { return "postgres"; });
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requests(model, &NavigatorModel::childrenRequested);
        controller.addConnection(9, "PostgreSQL");
        controller.setSelectedConnection(9);
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requests.count(), 1);
        QVERIFY(model->applyChildren(9, {}, requests.last().at(2).toULongLong(),
                                     {{"db", "database", "database", "database", true}}));
        const auto database = model->index(0, 0, root);
        model->fetchMore(database);
        QTRY_COMPARE(requests.count(), 2);
        QVERIFY(model->applyChildren(9, "db", requests.last().at(2).toULongLong(),
                                     {{"temp", "pg_temp_3", "pg_temp_3", "schema", true},
                                      {"public", "public", "public", "schema", false}}));
        filter.setText("pg_temp");
        // Filtering and searching wait for typing to pause.
        QTRY_COMPARE(tree.model()->rowCount(), 0);
        QCoreApplication::processEvents();
        QCOMPARE(requests.count(), 2);
        controller.setShowSystemSchemas(true);
        QTRY_COMPARE(requests.count(), 3);
        QCOMPARE(requests.last().at(1).toString(), QString("temp"));
        const auto lateToken = requests.last().at(2).toULongLong();
        controller.setShowSystemSchemas(false);
        QVERIFY(model->applyChildren(
            9, "temp", lateToken,
            {{"catalog-table", "tmp_result", "pg_temp_3.tmp_result", "table", false}}));
        QCoreApplication::processEvents();
        QCOMPARE(tree.model()->rowCount(), 0);
        QCOMPARE(model->completionSnapshot(9, {100, 4096, 100 * 8 + 64}).objects.size(), size_t(2));
        QCOMPARE(requests.count(), 3);
    }
    void lateHiddenFailureDoesNotInterruptVisibleSearch() {
        using namespace choscordb;
        EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        NavigatorController controller(&engine, &tree, &filter);
        controller.setDriverResolver([](quint64) { return "postgres"; });
        auto* model = controller.model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, &engine,
                            &EngineAdapter::loadMetadata);
        QSignalSpy requests(model, &NavigatorModel::childrenRequested);
        QSignalSpy status(&controller, &NavigatorController::searchStatusChanged);
        controller.addConnection(9, "PostgreSQL");
        controller.setSelectedConnection(9);
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(requests.count(), 1);
        QVERIFY(model->applyChildren(9, {}, requests.last().at(2).toULongLong(),
                                     {{"db", "database", "database", "database", true}}));
        const auto database = model->index(0, 0, root);
        model->fetchMore(database);
        QTRY_COMPARE(requests.count(), 2);
        QVERIFY(model->applyChildren(9, "db", requests.last().at(2).toULongLong(),
                                     {{"temp", "pg_temp_3", "pg_temp_3", "schema", true},
                                      {"public", "public", "public", "schema", true}}));
        controller.setShowSystemSchemas(true);
        model->fetchMore(model->index(0, 0, database));
        QTRY_COMPARE(requests.count(), 3);
        const auto hiddenToken = requests.last().at(2).toULongLong();
        controller.setShowSystemSchemas(false);
        filter.setText("public");
        QTRY_COMPARE(requests.count(), 4);
        QCOMPARE(requests.last().at(1).toString(), QString("public"));
        const auto visibleToken = requests.last().at(2).toULongLong();
        emit engine.metadataSubmissionFailed(9, "temp", hiddenToken, "Hidden error");
        QVERIFY(!status.last().first().toString().contains("Hidden error"));
        QVERIFY(model->applyChildren(9, "public", visibleToken, {}));
        QTRY_VERIFY(!status.isEmpty() && status.last().first().toString().isEmpty());
    }
    void metadataContinuationMenuRequestsTheNextBoundedPage() {
        choscordb::EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        choscordb::NavigatorController controller(&engine, &tree, &filter);
        auto* model = controller.model();
        QObject::disconnect(model, &choscordb::NavigatorModel::childrenRequested, &engine,
                            &choscordb::EngineAdapter::loadMetadata);
        QObject::disconnect(model, &choscordb::NavigatorModel::childrenPageRequested, &engine,
                            &choscordb::EngineAdapter::loadMetadataPage);
        QSignalSpy first(model, &choscordb::NavigatorModel::childrenRequested);
        QSignalSpy next(model, &choscordb::NavigatorModel::childrenPageRequested);
        controller.addConnection(9, "MySQL");
        const auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(first.count(), 1);
        QVERIFY(model->applyChildrenPage(9, {}, first.last().at(2).toULongLong(),
                                         {{"a", "Alpha", "db.Alpha", "table", false}}, 0, true, 1));
        QMenu menu;
        controller.populateContextMenu(&menu, model->index(1, 0, root));
        auto* more = menu.findChild<QAction*>("loadMoreMetadata");
        QVERIFY(more);
        QCOMPARE(menu.actions().size(), 1);
        more->trigger();
        QTRY_COMPARE(next.count(), 1);
        QCOMPARE(next.last().at(0).toULongLong(), quint64(9));
        QCOMPARE(next.last().at(3).toULongLong(), quint64(1));
        QCOMPARE(model->index(0, 0, root).data().toString(), QString("Alpha"));
    }

    void disconnectTargetsTheNodeAndRejectsAStaleMenu() {
        choscordb::EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        choscordb::NavigatorController controller(&engine, &tree, &filter);
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
        // Rust's 1 MiB template input limit, in three-byte characters.
        QVERIFY(!choscordb::SqlTemplateService::generate(
                     "select", QString(qsizetype(1024 * 1024 / 3 + 1), QChar(0x4e00)))
                     .valid);
        QVERIFY(
            !choscordb::SqlTemplateService::generate("insert", "\"t\"", {QString(QChar(0xd800))})
                 .valid);
    }
    void menuUsesLoadedColumnsAndRejectsRefreshAndRemoval() {
        choscordb::EngineAdapter engine;
        QTreeView tree;
        QLineEdit filter;
        choscordb::NavigatorController controller(&engine, &tree, &filter);
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
        QVERIFY(!unloaded.findChild<QAction*>("generate_update")->isEnabled());
        QVERIFY(unloaded.findChild<QAction*>("generate_update")->toolTip().contains("columns"));
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
        QVERIFY(loaded.findChild<QAction*>("generate_update")->isEnabled());
        loaded.findChild<QAction*>("generate_select")->trigger();
        QCOMPARE(generated.count(), 2);
        QVERIFY(generated.last().at(1).toString().startsWith("SELECT * FROM"));
        loaded.findChild<QAction*>("generate_insert")->trigger();
        QCOMPARE(generated.count(), 3);
        QVERIFY(generated.last().at(1).toString().contains("\"a.b\""));
        QCOMPARE(requested.count(), 2); // Generation never fetches metadata.
        model->refresh(table);
        loaded.findChild<QAction*>("generate_insert")->trigger();
        QCOMPARE(generated.count(), 3);
        QCOMPARE(failures.count(), 1);
        model->removeConnection(9);
        loaded.findChild<QAction*>("generate_select")->trigger();
        QCOMPARE(generated.count(), 3);
        QCOMPARE(failures.count(), 2);
    }
};
QTEST_MAIN(NavigatorSqlTest)
#include "navigator_sql_test.moc"
