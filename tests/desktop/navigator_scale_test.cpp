#include "app/navigator_controller.h"
#include "design_system/icons.h"
#include "models/navigator_model.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

using namespace choscordb;

namespace {
constexpr int kTables = 20000;

std::vector<NavigatorObject> tables(const QString& prefix = "t") {
    std::vector<NavigatorObject> rows;
    rows.reserve(kTables);
    for (int i = 0; i < kTables; ++i) {
        const auto name = prefix + QString::number(i);
        rows.emplace_back(name + "-id", name, "public." + name, "table", false);
    }
    return rows;
}

QModelIndex loadSchema(NavigatorModel& model, QSignalSpy& requested) {
    const auto connection = model.index(0, 0);
    model.fetchMore(connection);
    if (!requested.wait() && requested.isEmpty())
        return {};
    if (!model.applyChildren(42, {}, requested.last().at(2).toULongLong(),
                             {{"schema-id", "public", "public", "schema", true}}))
        return {};
    return model.index(0, 0, connection);
}
} // namespace

class NavigatorScaleTest : public QObject {
    Q_OBJECT
  private slots:
    void largeChildPagesKeepLookupsIndexed() {
        NavigatorModel model;
        QVERIFY(model.addConnection(42, "Warehouse"));
        QSignalSpy requested(&model, &NavigatorModel::childrenRequested);
        auto schema = loadSchema(model, requested);
        QVERIFY(schema.isValid());

        model.fetchMore(schema);
        QTRY_COMPARE(requested.size(), 2);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(
            model.applyChildren(42, "schema-id", requested.last().at(2).toULongLong(), tables()));
        QCOMPARE(model.rowCount(schema), kTables);
        // Every parent() and objectSnapshot() lookup must stay constant time.
        for (int row = 0; row < kTables; ++row)
            QCOMPARE(model.index(row, 0, schema).parent(), schema);
        const auto last = model.objectSnapshot(42, QString("t%1-id").arg(kTables - 1));
        QVERIFY(last.has_value());
        QCOMPARE(last->parentObjectId, QString("schema-id"));
        QVERIFY(model.matchesObject(42, "t0-id", "table", "public.t0", "schema-id"));
        // Generous bound: quadratic lookups take seconds at this size.
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));

        // Refreshing drops the old subtree from the index; reapplying the same IDs resolves anew.
        model.refresh(schema);
        QTRY_COMPARE(requested.size(), 3);
        QVERIFY(!model.objectSnapshot(42, "t0-id").has_value());
        QVERIFY(
            model.applyChildren(42, "schema-id", requested.last().at(2).toULongLong(), tables()));
        const auto first = model.objectSnapshot(42, "t0-id");
        QVERIFY(first.has_value());
        QCOMPARE(first->name, QString("t0"));
        QCOMPARE(model.index(kTables - 1, 0, schema).parent(), schema);

        QVERIFY(model.removeConnection(42));
        QVERIFY(!model.objectSnapshot(42, "t0-id").has_value());
    }

    void themedIconsAreCached() {
        const auto a = design::themedIcon(design::Icon::Table, QColor("#336699"), 16);
        const auto b = design::themedIcon(design::Icon::Table, QColor("#336699"), 16);
        const auto c = design::themedIcon(design::Icon::Table, QColor("#993366"), 16);
        QCOMPARE(a.cacheKey(), b.cacheKey());
        QVERIFY(a.cacheKey() != c.cacheKey());
        QVERIFY(!a.pixmap(16, 16).isNull());
    }

    void coalescedCallRunsLeadingAndOneTrailingCall() {
        QObject owner;
        int calls = 0;
        const auto call = coalescedCall(&owner, 50, [&calls] { ++calls; });
        call();
        QCOMPARE(calls, 1);
        for (int i = 0; i < 100; ++i)
            call();
        QCOMPARE(calls, 1);
        QTRY_COMPARE(calls, 2);
        QTest::qWait(120);
        QCOMPARE(calls, 2);
    }
};

QTEST_MAIN(NavigatorScaleTest)
#include "navigator_scale_test.moc"
