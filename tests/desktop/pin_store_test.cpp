#include "app/pin_store.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <utility>

class PinStoreTest : public QObject {
    Q_OBJECT

    static choscordb::PinRecord pin(QString profile, QString id, QString name,
                                    QString parent = QStringLiteral("[\"main\"]")) {
        return {.profileId = std::move(profile),
                .profileName = QStringLiteral("Saved SQLite"),
                .objectId = std::move(id),
                .name = name,
                .qualifiedName = QStringLiteral("\"main\".\"") + name + QLatin1Char('"'),
                .kind = QStringLiteral("table"),
                .parentObjectId = std::move(parent),
                .ancestryIds = {QStringLiteral("[\"main\"]")},
                .ancestryNames = {QStringLiteral("main")}};
    }

  private slots:
    void orderedRoundTripKeepsProfileAndObjectContext() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto profilePath = directory.filePath("profiles.sqlite");
        choscordb::PinStore store(profilePath);
        QString error;
        QVERIFY(store.load(&error).isEmpty());
        QVERIFY(error.isEmpty());

        auto newest = pin("profile-b", "[\"main\",\"orders\"]", "orders");
        newest.relationSubtype = "foreign_table";
        newest.unavailable = true;
        auto older = pin("profile-a", "[\"main\",\"orders\"]", "orders");
        QVERIFY2(store.save({newest, older}, &error), qPrintable(error));
        QCOMPARE(store.load(&error).size(), 2);
        const auto pins = choscordb::PinStore(profilePath).load(&error);
        QVERIFY(error.isEmpty());
        QCOMPARE(pins[0].profileId, QString("profile-b"));
        QCOMPARE(pins[0].profileName, QString("Saved SQLite"));
        QCOMPARE(pins[0].objectId, newest.objectId);
        QCOMPARE(pins[0].qualifiedName, newest.qualifiedName);
        QCOMPARE(pins[0].parentObjectId, newest.parentObjectId);
        QCOMPARE(pins[0].ancestryIds, newest.ancestryIds);
        QCOMPARE(pins[0].ancestryNames, newest.ancestryNames);
        QCOMPARE(pins[0].relationSubtype, QString("foreign_table"));
        QVERIFY(pins[0].unavailable);
        QCOMPARE(pins[1].profileId, QString("profile-a"));
        QVERIFY(!pins[1].unavailable);
        QVERIFY(QFile::exists(profilePath + ".pins.json"));
        QVERIFY(!QFile::exists(profilePath));
    }

    void duplicateLogicalObjectIsRejectedWithoutChangingSavedState() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        choscordb::PinStore store(directory.filePath("profiles.sqlite"));
        auto first = pin("saved", "pg:index:42", "orders_idx", "pg:group:1:index");
        first.kind = "index";
        QString error;
        QVERIFY2(store.save({first}, &error), qPrintable(error));
        auto sameObjectElsewhere = first;
        sameObjectElsewhere.parentObjectId = "pg:relation:24";
        sameObjectElsewhere.ancestryIds = {"pg:database:1", "pg:schema:2", "pg:relation:24"};
        sameObjectElsewhere.ancestryNames = {"database", "public", "orders"};
        QCOMPARE(choscordb::PinStore::identityKey(first),
                 choscordb::PinStore::identityKey(sameObjectElsewhere));
        QVERIFY(!store.save({first, sameObjectElsewhere}, &error));
        QVERIFY(!error.isEmpty());
        const auto loaded = store.load();
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded[0].parentObjectId, first.parentObjectId);

        sameObjectElsewhere.profileId = "other-saved";
        QVERIFY2(store.save({first, sameObjectElsewhere}, &error), qPrintable(error));
        QCOMPARE(store.load().size(), 2);
    }

    void rejectsNavigationRowsAndIncompleteOrUnsafeRecords() {
        auto candidate = pin("saved", "pg:relation:42", "orders");
        QVERIFY(choscordb::PinStore::valid(candidate));
        candidate.kind = "group";
        QVERIFY(!choscordb::PinStore::valid(candidate));
        candidate.kind = "connection";
        QVERIFY(!choscordb::PinStore::valid(candidate));
        candidate.kind = "primarykey";
        QVERIFY(choscordb::PinStore::valid(candidate));
        candidate.kind = "other";
        QVERIFY(!choscordb::PinStore::valid(candidate));
        candidate.kind = "primarykey";
        candidate.profileId.clear();
        QVERIFY(!choscordb::PinStore::valid(candidate));
        candidate.profileId = "saved";
        candidate.objectId = QString::fromLatin1("bad\0id", 6);
        QVERIFY(!choscordb::PinStore::valid(candidate));
        candidate.objectId = "pg:constraint:42";
        candidate.ancestryIds = {QString(QChar(0xd800))};
        QVERIFY(!choscordb::PinStore::valid(candidate));
    }

    void directRootSchemaHasValidSavedIdentity() {
        auto schema = pin("mysql-profile", "mysql:schema:analytics", "analytics");
        schema.kind = "schema";
        schema.qualifiedName = "`analytics`";
        schema.parentObjectId.clear();
        schema.ancestryIds.clear();
        schema.ancestryNames.clear();
        QVERIFY(choscordb::PinStore::valid(schema));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        choscordb::PinStore store(directory.filePath("profiles.sqlite"));
        QString error;
        QVERIFY2(store.save({schema}, &error), qPrintable(error));
        const auto restored = store.load(&error);
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored.first().parentObjectId, QString());
        QCOMPARE(restored.first().kind, QString("schema"));
    }

    void malformedAndUnsupportedDataCannotCreateActionablePins() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("metadata.sqlite");
        QFile file(path + ".pins.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray malformed =
            "{\"version\":1,\"pins\":["
            "{\"profileId\":\"saved\",\"profileName\":\"Saved\",\"objectId\":\"pg:relation:42\","
            "\"name\":\"orders\",\"qualifiedName\":\"orders\",\"kind\":\"table\","
            "\"parentObjectId\":\"pg:group:3:table\",\"ancestryIds\":[],"
            "\"ancestryNames\":[],\"relationSubtype\":\"\",\"unavailable\":false},"
            "{\"profileId\":\"saved\",\"profileName\":\"Saved\",\"objectId\":\"pg:relation:42\","
            "\"name\":\"orders\",\"qualifiedName\":\"orders\",\"kind\":\"table\","
            "\"parentObjectId\":\"pg:relation:99\",\"ancestryIds\":[],"
            "\"ancestryNames\":[],\"relationSubtype\":\"\",\"unavailable\":false},"
            "{\"profileId\":\"saved\",\"profileName\":\"Saved\",\"objectId\":\"unsafe\","
            "\"name\":\"group\",\"qualifiedName\":\"group\",\"kind\":\"group\","
            "\"parentObjectId\":\"x\",\"ancestryIds\":[],\"ancestryNames\":[],"
            "\"relationSubtype\":\"\",\"unavailable\":false}]}";
        QCOMPARE(file.write(malformed), malformed.size());
        file.close();
        choscordb::PinStore store(path);
        QString error;
        const auto loaded = store.load(&error);
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded[0].objectId, QString("pg:relation:42"));
        QVERIFY(!error.isEmpty());

        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray unsupported = "{\"version\":99,\"pins\":[]}";
        QCOMPARE(file.write(unsupported), unsupported.size());
        file.close();
        QVERIFY(store.load(&error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void failedWriteReportsErrorAndNeverReplacesPriorPins() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        choscordb::PinStore store(directory.filePath("profiles.sqlite"));
        const auto original = pin("saved", "[\"main\",\"orders\"]", "orders");
        QString error;
        QVERIFY2(store.save({original}, &error), qPrintable(error));
        auto invalid = original;
        invalid.kind = "loading";
        QVERIFY(!store.save({invalid}, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(store.load()[0].objectId, original.objectId);

        const auto blockedPath = directory.filePath("blocked.sqlite.pins.json");
        QVERIFY(QDir().mkpath(blockedPath));
        choscordb::PinStore blocked(directory.filePath("blocked.sqlite"));
        QVERIFY(!blocked.save({original}, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(PinStoreTest)
#include "pin_store_test.moc"
