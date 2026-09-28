#include "app/object_action_sql.h"
#include <QtTest>

class ObjectActionSqlTest : public QObject {
    Q_OBJECT

  private slots:
    void dropQuotesTrustedIdentityForEachDriver() {
        using choscordb::ObjectActionSql;
        const auto sqlite = ObjectActionSql::drop("sqlite", "table", "[\"main\",\"a.\\\"b\"]",
                                                  "untrusted display text");
        QVERIFY2(sqlite.valid, qPrintable(sqlite.error));
        QCOMPARE(sqlite.sql, QStringLiteral("DROP TABLE \"main\".\"a.\"\"b\";"));

        const auto postgres = ObjectActionSql::drop("postgres", "view", "pg:relation:42",
                                                    "\"public\".\"order\"");
        QVERIFY2(postgres.valid, qPrintable(postgres.error));
        QCOMPARE(postgres.sql, QStringLiteral("DROP VIEW \"public\".\"order\";"));

        const auto mysql = ObjectActionSql::drop("mysql", "table", "[\"db\",\"a`b\"]",
                                                 "untrusted display text");
        QVERIFY2(mysql.valid, qPrintable(mysql.error));
        QCOMPARE(mysql.sql, QStringLiteral("DROP TABLE `db`.`a``b`;"));
    }

    void renameKeepsSchemaAndEscapesNewShortName() {
        using choscordb::ObjectActionSql;
        const auto sqlite = ObjectActionSql::rename("sqlite", "table",
                                                    "[\"aux\",\"old.name\"]", "ignored",
                                                    "new\"name");
        QVERIFY2(sqlite.valid, qPrintable(sqlite.error));
        QCOMPARE(sqlite.sql,
                 QStringLiteral("ALTER TABLE \"aux\".\"old.name\" RENAME TO \"new\"\"name\";"));
        QCOMPARE(sqlite.newObjectId, QStringLiteral("[\"aux\",\"new\\\"name\"]"));
        QCOMPARE(sqlite.newQualifiedName, QStringLiteral("\"aux\".\"new\"\"name\""));
        const auto injected = ObjectActionSql::rename("sqlite", "table",
                                                      "[\"aux\",\"old.name\"]", "ignored",
                                                      "x\"; DROP TABLE victims;--");
        QVERIFY2(injected.valid, qPrintable(injected.error));
        QCOMPARE(injected.sql, QStringLiteral(
                                   "ALTER TABLE \"aux\".\"old.name\" RENAME TO "
                                   "\"x\"\"; DROP TABLE victims;--\";"));

        const auto postgres = ObjectActionSql::rename("postgres", "view", "pg:relation:42",
                                                      "\"odd.schema\".\"a\"\"b\"", "select.x");
        QVERIFY2(postgres.valid, qPrintable(postgres.error));
        QCOMPARE(postgres.sql, QStringLiteral(
                                   "ALTER VIEW \"odd.schema\".\"a\"\"b\" RENAME TO \"select.x\";"));
        QCOMPARE(postgres.newObjectId, QStringLiteral("pg:relation:42"));
        QCOMPARE(postgres.newQualifiedName, QStringLiteral("\"odd.schema\".\"select.x\""));

        const auto mysql = ObjectActionSql::rename("mysql", "view", "[\"db\",\"order\"]",
                                                   "ignored", "x`y");
        QVERIFY2(mysql.valid, qPrintable(mysql.error));
        QCOMPARE(mysql.sql, QStringLiteral("RENAME TABLE `db`.`order` TO `db`.`x``y`;"));
        QCOMPARE(mysql.newObjectId, QStringLiteral("[\"db\",\"x`y\"]"));
        QCOMPARE(mysql.newQualifiedName, QStringLiteral("`db`.`x``y`"));
    }

    void postgresSpecialRelationsUseTheirOwnDdl() {
        using choscordb::ObjectActionSql;
        const auto materializedDrop =
            ObjectActionSql::drop("postgres", "view", "pg:relation:42", "\"s\".\"v\"",
                                  "materialized_view");
        QVERIFY2(materializedDrop.valid, qPrintable(materializedDrop.error));
        QCOMPARE(materializedDrop.sql, QStringLiteral("DROP MATERIALIZED VIEW \"s\".\"v\";"));
        const auto materializedRename = ObjectActionSql::rename(
            "postgres", "view", "pg:relation:42", "\"s\".\"v\"", "new.name", "materialized_view");
        QVERIFY2(materializedRename.valid, qPrintable(materializedRename.error));
        QCOMPARE(materializedRename.sql,
                 QStringLiteral("ALTER MATERIALIZED VIEW \"s\".\"v\" RENAME TO \"new.name\";"));
        QCOMPARE(materializedRename.newObjectId, QStringLiteral("pg:relation:42"));
        QCOMPARE(materializedRename.newQualifiedName, QStringLiteral("\"s\".\"new.name\""));

        const auto foreignDrop =
            ObjectActionSql::drop("postgres", "table", "pg:relation:84", "\"s\".\"t\"",
                                  "foreign_table");
        QVERIFY2(foreignDrop.valid, qPrintable(foreignDrop.error));
        QCOMPARE(foreignDrop.sql, QStringLiteral("DROP FOREIGN TABLE \"s\".\"t\";"));
        const auto foreignRename = ObjectActionSql::rename(
            "postgres", "table", "pg:relation:84", "\"s\".\"t\"", "new.name", "foreign_table");
        QVERIFY2(foreignRename.valid, qPrintable(foreignRename.error));
        QCOMPARE(foreignRename.sql,
                 QStringLiteral("ALTER FOREIGN TABLE \"s\".\"t\" RENAME TO \"new.name\";"));
        QCOMPARE(foreignRename.newObjectId, QStringLiteral("pg:relation:84"));
        QCOMPARE(foreignRename.newQualifiedName, QStringLiteral("\"s\".\"new.name\""));
    }

    void invalidNamesAndIdentitiesCannotProduceSql() {
        using choscordb::ObjectActionSql;
        const auto sqliteId = QStringLiteral("[\"main\",\"orders\"]");
        for (const auto& invalid : {QString{}, QStringLiteral("orders"),
                                    QStringLiteral("  "), QString::fromLatin1("bad\0name", 8),
                                    QString(QChar(0xd800))}) {
            const auto statement =
                ObjectActionSql::rename("sqlite", "table", sqliteId, "ignored", invalid);
            QVERIFY(!statement.valid);
            QVERIFY(statement.sql.isEmpty());
        }
        QVERIFY(!ObjectActionSql::rename("sqlite", "view", sqliteId, "ignored", "other").valid);
        QVERIFY(!ObjectActionSql::rename("sqlite", "table", sqliteId, "ignored", "SQLite_foo")
                     .valid);
        const auto nearbyValid =
            ObjectActionSql::rename("sqlite", "table", sqliteId, "ignored", "sqlitefoo");
        QVERIFY2(nearbyValid.valid, qPrintable(nearbyValid.error));
        QCOMPARE(nearbyValid.sql,
                 QStringLiteral("ALTER TABLE \"main\".\"orders\" RENAME TO \"sqlitefoo\";"));
        QVERIFY(!ObjectActionSql::drop("sqlite", "index", sqliteId, "ignored").valid);
        QVERIFY(!ObjectActionSql::drop("sqlite", "table", "[\"main\",\"a\",\"b\"]", "ignored")
                     .valid);
        QVERIFY(!ObjectActionSql::drop("sqlite", "table", "not-json", "ignored").valid);
        QVERIFY(!ObjectActionSql::drop("postgres", "table", "pg:relation:42",
                                       "\"main\".\"safe\";DROP TABLE x;")
                     .valid);
        QVERIFY(!ObjectActionSql::drop("postgres", "table", "pg:relation:0",
                                       "\"main\".\"safe\"")
                     .valid);
        QVERIFY(!ObjectActionSql::rename("postgres", "table", "pg:relation:42",
                                         "\"main\".\"safe\"", QString(64, QChar('x')))
                     .valid);
        QVERIFY(!ObjectActionSql::rename("mysql", "table", "[\"db\",\"old\"]", "ignored",
                                         QString(65, QChar('x')))
                     .valid);
        QVERIFY(!ObjectActionSql::rename("mysql", "table", "[\"db\",\"old\"]", "ignored",
                                         "trailing ")
                     .valid);
        QVERIFY(!ObjectActionSql::rename("mysql", "table", "[\"db\",\"old\"]", "ignored",
                                         QString::fromUtf8("😀"))
                     .valid);
        QVERIFY(!ObjectActionSql::drop("postgres", "table", "pg:relation:42", "\"s\".\"v\"",
                                       "materialized_view")
                     .valid);
        QVERIFY(!ObjectActionSql::drop("postgres", "view", "pg:relation:42", "\"s\".\"v\"",
                                       "foreign_table")
                     .valid);
        QVERIFY(!ObjectActionSql::drop("sqlite", "table", sqliteId, "ignored",
                                       "foreign_table")
                     .valid);
        QVERIFY(!ObjectActionSql::drop("postgres", "view", "pg:relation:42", "\"s\".\"v\"",
                                       "unknown")
                     .valid);
    }
};

QTEST_GUILESS_MAIN(ObjectActionSqlTest)
#include "object_action_sql_test.moc"
