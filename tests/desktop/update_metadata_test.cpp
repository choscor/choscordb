#include "app/update_metadata.h"
#include "app/update_readiness.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class UpdateMetadataTest : public QObject {
    Q_OBJECT
  private:
    static QByteArray envelope() {
        QJsonObject json;
        json.insert("key_id", "windows-linux-v1");
        json.insert("payload",
                    "eyJ2ZXJzaW9uIjoiMS4yLjQiLCJwbGF0Zm9ybSI6ImxpbnV4IiwiYXJjaCI6Ing4Nl82NCIsInVybC"
                    "I6Imh0dHBzOi8vZ2l0aHViLmNvbS9jaG9zY29yL2Nob3Njb3JkYi9yZWxlYXNlcy9kb3dubG9hZC92"
                    "MS4yLjQvQ2hvc2NvckRCLTEuMi40LWxpbnV4LXg4Nl82NC5BcHBJbWFnZSIsInNpemUiOjQsInNoYT"
                    "I1NiI6Ijg4ZDQyNjZmZDRlNjMzOGQxM2I4NDVmY2YyODk1NzlkMjA5Yzg5NzgyM2I5MjE3ZGEzZTE2"
                    "MTkzNmYwMzE1ODkiLCJub3RlcyI6IlN0YWJsZSBpbXByb3ZlbWVudHMifQ==");
        json.insert("signature",
                    "t0FQGcniz1p3Qk5S/"
                    "2YzFX5NmMDH0Nzf53qMPwvGvjOi5lSgBg1wYzSZcF+Wxo7rqoMGZqcrNCwDv1JjfcUtDQ==");
        return QJsonDocument(json).toJson(QJsonDocument::Compact);
    }
    static QByteArray key() {
        return QByteArray::fromBase64("ebVWLo/mVPlAeLES6KmLp5AfhTrmlb7X4OORC60ElmQ=");
    }
  private slots:
    void acceptsSignedNewStableReleaseAndVerifiedBytes() {
        QString error;
        auto record = choscordb::parseSignedUpdateMetadata(envelope(), key(), "1.2.3", "linux",
                                                           "x86_64", "choscor/choscordb", &error);
        QVERIFY2(record.has_value(), qPrintable(error));
        QCOMPARE(record->version, "1.2.4");
        QCOMPARE(record->notes, "Stable improvements");
        QTemporaryDir dir;
        QFile file(dir.filePath("package"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("abcd"), 4);
        file.close();
        QVERIFY2(choscordb::verifyUpdateFile(file.fileName(), *record, &error), qPrintable(error));
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.seek(3));
        QCOMPARE(file.write("e"), 1);
        file.close();
        QVERIFY(!choscordb::verifyUpdateFile(file.fileName(), *record, &error));
        const auto emptyPath = dir.filePath("empty-package");
        QFile empty(emptyPath);
        QVERIFY(empty.open(QIODevice::WriteOnly));
        empty.close();
        auto invalid = *record;
        invalid.size = -1;
        invalid.sha256 =
            QByteArray::fromHex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        QVERIFY(!choscordb::verifyUpdateFile(emptyPath, invalid, &error));
        QCOMPARE(error, "Downloaded package failed integrity verification");
        QVERIFY(file.open(QIODevice::Append));
        QCOMPARE(file.write("x"), 1);
        file.close();
        QVERIFY(!choscordb::verifyUpdateFile(file.fileName(), *record, &error));
    }
    void acceptsSignedWindowsInstallerName() {
        QJsonObject json;
        json.insert("key_id", "windows-linux-v1");
        json.insert("payload",
                    "eyJ2ZXJzaW9uIjoiMS4yLjQiLCJwbGF0Zm9ybSI6IndpbmRvd3MiLCJhcmNoIjoieDY0IiwidXJsIj"
                    "oiaHR0cHM6Ly9naXRodWIuY29tL2Nob3Njb3IvY2hvc2NvcmRiL3JlbGVhc2VzL2Rvd25sb2FkL3Yx"
                    "LjIuNC9DaG9zY29yREItMS4yLjQtd2luZG93cy14NjQtc2V0dXAuZXhlIiwic2l6ZSI6NCwic2hhMj"
                    "U2IjoiODhkNDI2NmZkNGU2MzM4ZDEzYjg0NWZjZjI4OTU3OWQyMDljODk3ODIzYjkyMTdkYTNlMTYx"
                    "OTM2ZjAzMTU4OSIsIm5vdGVzIjoiV2luZG93cyBzdGFibGUifQ==");
        json.insert(
            "signature",
            "dVZ+"
            "bRkBSbgxm05VsMTRTULH1PzNgQw4Ck4wMsbdBB65VGRNNhZe6MMCSWabJ9PwFsFj42PmFpbPujdszRHqDg==");
        QString error;
        auto record = choscordb::parseSignedUpdateMetadata(
            QJsonDocument(json).toJson(QJsonDocument::Compact), key(), "1.2.3", "windows", "x64",
            "choscor/choscordb", &error);
        QVERIFY2(record.has_value(), qPrintable(error));
        QCOMPARE(record->url.fileName(), "ChoscorDB-1.2.4-windows-x64-setup.exe");
    }
    void rejectsTamperedAndMismatchedFeeds() {
        QString error;
        auto wrapper = QJsonDocument::fromJson(envelope()).object();
        auto payload = QByteArray::fromBase64(wrapper.value("payload").toString().toLatin1());
        payload.replace("\"size\":4", "\"size\":5");
        wrapper.insert("payload", QString::fromLatin1(payload.toBase64()));
        QVERIFY(!choscordb::parseSignedUpdateMetadata(
            QJsonDocument(wrapper).toJson(QJsonDocument::Compact), key(), "1.2.3", "linux",
            "x86_64", "choscor/choscordb", &error));
        QCOMPARE(error, "Update signature is invalid");
        wrapper = QJsonDocument::fromJson(envelope()).object();
        wrapper.insert("key_id", "unknown-key");
        QVERIFY(!choscordb::parseSignedUpdateMetadata(
            QJsonDocument(wrapper).toJson(QJsonDocument::Compact), key(), "1.2.3", "linux",
            "x86_64", "choscor/choscordb", &error));
        QCOMPARE(error, "Unsupported update signing key");
        QVERIFY(!choscordb::parseSignedUpdateMetadata(envelope(), key(), "1.2.3", "windows", "x64",
                                                      "choscor/choscordb", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!choscordb::parseSignedUpdateMetadata(envelope(), key(), "1.2.3", "linux", "arm64",
                                                      "choscor/choscordb", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!choscordb::parseSignedUpdateMetadata(envelope(), key(), "1.2.3", "linux", "x86_64",
                                                      "fork/project", &error));
        QCOMPARE(error, "Unexpected update package URL");
        QVERIFY(!choscordb::parseSignedUpdateMetadata(envelope(), key(), "1.2.4", "linux", "x86_64",
                                                      "choscor/choscordb", &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!choscordb::parseSignedUpdateMetadata(envelope(), key(), "1.2.5", "linux", "x86_64",
                                                      "choscor/choscordb", &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!choscordb::parseSignedUpdateMetadata(
            envelope(), key(), "1.2.1000000000000000000000000000000000000", "linux", "x86_64",
            "choscor/choscordb", &error));
        QVERIFY(error.isEmpty());
    }
    void readinessMarkerIsExclusiveAndConstrainedToTempDirectory() {
        const auto temp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        const auto suffix = QString::number(QRandomGenerator::global()->generate64(), 16);
        const auto path =
            QDir(temp).filePath(QStringLiteral("ChoscorDB-update-ready-%1.txt").arg(suffix));
        QFile::remove(path);
        QVERIFY(choscordb::writeUpdateReadinessFile(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("ready\n"));
        file.close();
        QVERIFY(!choscordb::writeUpdateReadinessFile(path));
        QVERIFY(QFile::remove(path));
        QVERIFY(!choscordb::writeUpdateReadinessFile(QDir(temp).filePath("unrelated.txt")));
        QTemporaryDir unrelated;
        QVERIFY(unrelated.isValid());
        QVERIFY(!choscordb::writeUpdateReadinessFile(
            unrelated.filePath(QStringLiteral("ChoscorDB-update-ready-%1.txt").arg(suffix))));
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    UpdateMetadataTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "update_metadata_test.moc"
