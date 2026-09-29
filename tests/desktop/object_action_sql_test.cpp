#include "app/object_action_sql.h"
#include <QtTest>

class ObjectActionSqlTest : public QObject {
    Q_OBJECT

  private slots:
    void unicodeRoundTripsThroughRustBridge() {
        const auto statement = choscordb::ObjectActionSql::rename(
            "sqlite", "table", QStringLiteral("[\"main\",\"café\"]"), "ignored",
            QStringLiteral("étude"));
        QVERIFY2(statement.valid, qPrintable(statement.error));
        QCOMPARE(statement.sql,
                 QStringLiteral("ALTER TABLE \"main\".\"café\" RENAME TO \"étude\";"));
        QCOMPARE(statement.newObjectId, QStringLiteral("[\"main\",\"étude\"]"));
        QCOMPARE(statement.newQualifiedName, QStringLiteral("\"main\".\"étude\""));
    }

    void invalidUtf16DoesNotChangeDuringTransport() {
        const auto statement = choscordb::ObjectActionSql::rename(
            "sqlite", "table", "[\"main\",\"orders\"]", "ignored", QString(QChar(0xd800)));
        QVERIFY(!statement.valid);
        QVERIFY(statement.sql.isEmpty());
        QVERIFY(!statement.error.isEmpty());
    }

    void rustErrorIsPresentedByBridge() {
        const auto statement =
            choscordb::ObjectActionSql::drop("sqlite", "table", "not-json", "ignored");
        QVERIFY(!statement.valid);
        QVERIFY(statement.sql.isEmpty());
        QCOMPARE(statement.error, QStringLiteral("The selected object has an invalid identity."));
    }
};

QTEST_GUILESS_MAIN(ObjectActionSqlTest)
#include "object_action_sql_test.moc"
