#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <QSignalSpy>
#include <QTest>

using namespace choscordb;
class ObjectInspectionAdapterTest final : public QObject {
    Q_OBJECT
  private slots:
    void realMetadataEmptyFailureRetryAndLatestRequest() {
        EngineAdapter adapter;
        bool connected = false;
        int finished = 0;
        connect(&adapter, &EngineAdapter::eventReady, this, [&](const BridgeEvent& event) {
            if (event.kind == "connected")
                connected = true;
            if (event.kind == "query_finished")
                ++finished;
        });
        auto connection = adapter.connectSqlite(":memory:");
        QVERIFY(connection);
        QTRY_VERIFY(connected);
        const auto create = adapter.execute(
            *connection, "CREATE TABLE \"dữ liệu\"(label TEXT NOT NULL DEFAULT 'actual')");
        QVERIFY(create);
        adapter.fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        QSignalSpy ready(&adapter, &EngineAdapter::objectInspectionReady);
        QSignalSpy failed(&adapter, &EngineAdapter::objectInspectionFailed);
        const QString object = R"(["main","dữ liệu"])";
        adapter.loadObjectInspection(*connection, object, ObjectInspectionPane::Columns, 11);
        QTRY_COMPARE(ready.count(), 1);
        const auto columns = qvariant_cast<ObjectInspection>(ready.takeFirst().at(3));
        QCOMPARE(columns.rows.size(), 1);
        QCOMPARE(columns.rows.first().name, QString("label"));
        QString defaultValue, nullable;
        for (const auto& property : columns.rows.first().properties) {
            if (property.name == "Default")
                defaultValue = property.value;
            if (property.name == "Nullable")
                nullable = property.value;
        }
        QCOMPARE(defaultValue, QString("'actual'"));
        QCOMPARE(nullable, QString("No"));
        adapter.loadObjectInspection(*connection, object, ObjectInspectionPane::Indexes, 12);
        QTRY_COMPARE(ready.count(), 1);
        const auto indexes = qvariant_cast<ObjectInspection>(ready.takeFirst().at(3));
        QCOMPARE(indexes.availability, MetadataAvailability::Available);
        QVERIFY(indexes.rows.isEmpty());
        adapter.loadObjectInspection(*connection, R"(["main"])", ObjectInspectionPane::Ddl, 13);
        QTRY_COMPARE(ready.count(), 1);
        const auto unsupported = qvariant_cast<ObjectInspection>(ready.takeFirst().at(3));
        QCOMPARE(unsupported.availability, MetadataAvailability::Unsupported);
        QVERIFY(!unsupported.reason.isEmpty());
        adapter.loadObjectInspection(*connection, R"(["main","missing"])",
                                     ObjectInspectionPane::Ddl, 14);
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.takeFirst().at(2).toULongLong(), quint64(14));
        adapter.loadObjectInspection(*connection, object, ObjectInspectionPane::Ddl, 15);
        QTRY_COMPARE(ready.count(), 1);
        QVERIFY(qvariant_cast<ObjectInspection>(ready.takeFirst().at(3))
                    .ddl.contains("DEFAULT 'actual'"));
        adapter.loadObjectInspection(*connection, R"(["main","missing"])",
                                     ObjectInspectionPane::Columns, 16);
        adapter.loadObjectInspection(*connection, object, ObjectInspectionPane::Columns, 17);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.at(0).at(2).toULongLong(), quint64(17));
        QCOMPARE(failed.count(), 0);
    }
};
QTEST_MAIN(ObjectInspectionAdapterTest)
#include "object_inspection_adapter_test.moc"
