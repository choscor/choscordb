#include "app/object_action_sql.h"
#include <QtTest>

namespace {
choscordb::ObjectActionRequest request(const QString& action, const QString& driver,
                                       const QString& objectId, const QString& newName = {}) {
    return {.action = action,
            .driver = driver,
            .kind = QStringLiteral("table"),
            .objectId = objectId,
            .qualifiedName = QStringLiteral("ignored"),
            .newName = newName};
}
}

class ObjectActionSqlTest : public QObject {
    Q_OBJECT

  private slots:
    void unicodeRoundTripsThroughRustBridge() {
        const auto statement = choscordb::ObjectActionSql::prepare(request(
            "rename", "sqlite", QStringLiteral("[\"main\",\"café\"]"), QStringLiteral("étude")));
        QVERIFY2(statement.valid, qPrintable(statement.error));
        QCOMPARE(statement.sql,
                 QStringLiteral("ALTER TABLE \"main\".\"café\" RENAME TO \"étude\";"));
        QCOMPARE(statement.newObjectId, QStringLiteral("[\"main\",\"étude\"]"));
        QCOMPARE(statement.newQualifiedName, QStringLiteral("\"main\".\"étude\""));
        QCOMPARE(statement.newName, QStringLiteral("étude"));
    }

    void invalidUtf16DoesNotChangeDuringTransport() {
        const auto statement = choscordb::ObjectActionSql::prepare(
            request("rename", "sqlite", "[\"main\",\"orders\"]", QString(QChar(0xd800))));
        QVERIFY(!statement.valid);
        QVERIFY(statement.sql.isEmpty());
        QVERIFY(!statement.error.isEmpty());
    }

    void rustErrorIsPresentedByBridge() {
        const auto statement =
            choscordb::ObjectActionSql::prepare(request("drop", "sqlite", "not-json"));
        QVERIFY(!statement.valid);
        QVERIFY(statement.sql.isEmpty());
        QCOMPARE(statement.error, QStringLiteral("The selected object has an invalid identity."));
    }

    void cloneReturnsEachStatementInOrder() {
        auto clone = request("clone", "mysql", "[\"shop\",\"orders\"]", "orders_copy");
        clone.withData = true;
        const auto statement = choscordb::ObjectActionSql::prepare(clone);
        QVERIFY2(statement.valid, qPrintable(statement.error));
        QCOMPARE(statement.statements,
                 QStringList({"CREATE TABLE `shop`.`orders_copy` LIKE `shop`.`orders`;",
                              "INSERT INTO `shop`.`orders_copy` SELECT * FROM `shop`.`orders`;"}));
    }

    void scriptsComeFromRust() {
        QStringList codes;
        for (const auto& script : choscordb::ObjectActionSql::scripts())
            codes << script.code;
        QCOMPARE(codes, QStringList({"create", "drop", "truncate", "select"}));
        const auto select = choscordb::ObjectActionSql::script(
            "select", "sqlite", "table", "[\"main\",\"orders\"]", "ignored", {});
        QVERIFY(select.valid && !select.fromDdl);
        QCOMPARE(select.sql, QStringLiteral("SELECT * FROM \"main\".\"orders\" LIMIT 100;"));
        QVERIFY(choscordb::ObjectActionSql::script("create", "sqlite", "table",
                                                   "[\"main\",\"orders\"]", "ignored", {})
                    .fromDdl);
    }
};

QTEST_GUILESS_MAIN(ObjectActionSqlTest)
#include "object_action_sql_test.moc"
