#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <QtTest>
class QueryOptionsTest : public QObject {
    Q_OBJECT
  private slots:
    void timeoutStopsWorkAndTheConnectionRemainsUsable() {
        choscordb::EngineAdapter adapter;
        bool connected = false;
        QString failure;
        QList<qint64> values;
        connect(
            &adapter, &choscordb::EngineAdapter::eventReady, &adapter,
            [&](const choscordb::BridgeEvent& event) {
                const auto kind =
                    QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
                if (kind == "connected")
                    connected = true;
                if (kind == "query_failed") {
                    failure = QString::fromUtf8(event.error_kind.data(),
                                                qsizetype(event.error_kind.size()));
                }
                if (kind == "stored_page" && !event.cells.empty())
                    values.append(event.cells[0].integer);
            },
            Qt::DirectConnection);
        const auto connection = adapter.connectSqlite(":memory:");
        QVERIFY(connection);
        QTRY_VERIFY(connected);
        choscordb::QueryPreferences preferences;
        preferences.timeoutSeconds = 1;
        const auto query = adapter.execute(*connection,
                                           "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 "
                                           "FROM n WHERE x<100000000) SELECT sum(x) FROM n",
                                           true, {}, preferences);
        QVERIFY(query);
        adapter.fetchPageAt(*query, 0);
        QTRY_COMPARE_WITH_TIMEOUT(failure, QString("Timeout"), 5000);
        preferences.timeoutSeconds = 0;
        const auto recovered = adapter.execute(*connection, "SELECT 7", true, {}, preferences);
        QVERIFY(recovered);
        adapter.fetchPageAt(*recovered, 0);
        QTRY_COMPARE(values.size(), 1);
        QCOMPARE(values.first(), qint64(7));
    }
    void maximumPageSizeAndRemainderAreHonored() {
        choscordb::EngineAdapter adapter;
        bool connected = false;
        QList<quint32> counts;
        connect(
            &adapter, &choscordb::EngineAdapter::eventReady, &adapter,
            [&](const choscordb::BridgeEvent& event) {
                const auto kind =
                    QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
                if (kind == "connected")
                    connected = true;
                if (kind == "stored_page")
                    counts.append(event.row_count);
            },
            Qt::DirectConnection);
        auto connection = adapter.connectSqlite(":memory:");
        QVERIFY(connection);
        QTRY_VERIFY(connected);
        choscordb::QueryPreferences preferences;
        preferences.pageSize = 10000;
        auto query = adapter.execute(*connection,
                                     "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n "
                                     "WHERE x<10001) SELECT x FROM n",
                                     true, {}, preferences);
        QVERIFY(query);
        adapter.fetchPageAt(*query, 0);
        QTRY_COMPARE(counts.size(), 1);
        QCOMPARE(counts.first(), quint32(10000));
        adapter.fetchPageAt(*query, 1);
        QTRY_COMPARE(counts.size(), 2);
        QCOMPARE(counts.last(), quint32(1));
    }
    void pageSizeIsCapturedPerExecutionAndUsedForArchivedReads() {
        choscordb::EngineAdapter adapter;
        bool connected = false;
        struct Page {
            quint64 query, index;
            quint32 rows;
            qint64 first;
        };
        QList<Page> pages;
        connect(
            &adapter, &choscordb::EngineAdapter::eventReady, &adapter,
            [&](const choscordb::BridgeEvent& event) {
                const auto kind =
                    QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
                if (kind == "connected")
                    connected = true;
                if (kind == "stored_page")
                    pages.append({event.id, event.page_index, event.row_count,
                                  event.cells.empty() ? 0 : event.cells[0].integer});
            },
            Qt::DirectConnection);
        const auto connection = adapter.connectSqlite(":memory:");
        QVERIFY(connection);
        QTRY_VERIFY(connected);
        const QString sql = "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                            "x<750) SELECT x FROM n";
        choscordb::QueryPreferences preferences;
        preferences.pageSize = 100;
        const auto first = adapter.execute(*connection, sql, true, {}, preferences);
        QVERIFY(first);
        adapter.fetchPageAt(*first, 0);
        QTRY_COMPARE(pages.size(), 1);
        QCOMPARE(pages.last().rows, quint32(100));
        preferences.pageSize = 250;
        adapter.fetchPageAt(*first, 1);
        QTRY_COMPARE(pages.size(), 2);
        QCOMPARE(pages.last().rows, quint32(100));
        QCOMPARE(pages.last().first, qint64(101));
        adapter.fetchPageAt(*first, 0);
        QTRY_COMPARE(pages.size(), 3);
        QCOMPARE(pages.last().first, qint64(1));
        const auto second = adapter.execute(*connection, sql, true, {}, preferences);
        QVERIFY(second);
        adapter.fetchPageAt(*second, 0);
        QTRY_COMPARE(pages.size(), 4);
        QCOMPARE(pages.last().rows, quint32(250));
        adapter.fetchPageAt(*first, 1);
        QTRY_COMPARE(pages.size(), 5);
        QCOMPARE(pages.last().rows, quint32(100));
        QCOMPARE(pages.last().first, qint64(101));
    }
};
QTEST_MAIN(QueryOptionsTest)
#include "query_options_test.moc"
