#include "app/diagnostics_file_lock.h"
#include "app/diagnostics_service.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <iostream>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
quint16 le16(const QByteArray& bytes, qsizetype offset) {
    return quint8(bytes.at(offset)) | (quint16(quint8(bytes.at(offset + 1))) << 8);
}
quint32 le32(const QByteArray& bytes, qsizetype offset) {
    return quint32(le16(bytes, offset)) | (quint32(le16(bytes, offset + 2)) << 16);
}
QByteArray zipMember(const QString& path, const QByteArray& wanted) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto bytes = file.readAll();
    qsizetype cursor = 0;
    while (cursor + 30 <= bytes.size() && le32(bytes, cursor) == 0x04034b50) {
        const auto size = le32(bytes, cursor + 18);
        const auto nameSize = le16(bytes, cursor + 26);
        const auto extraSize = le16(bytes, cursor + 28);
        const auto begin = cursor + 30 + nameSize + extraSize;
        if (begin + size > bytes.size())
            return {};
        if (bytes.mid(cursor + 30, nameSize) == wanted)
            return bytes.mid(begin, size);
        cursor = begin + size;
    }
    return {};
}
QByteArray fixtureLine(const QDate& day, const QString& event, const QString& extra = {}) {
    QJsonObject object{
        {"schema", 1},
        {"utc", QDateTime(day, QTime(12, 0), QTimeZone::UTC).toString(Qt::ISODateWithMs)},
        {"event", event},
        {"driver", "unknown"},
        {"error_class", "unknown"},
        {"duration_bucket", "unknown"},
        {"open_tabs", 0}};
    if (!extra.isEmpty())
        object.insert("sql", extra);
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}
bool writeBytes(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
} // namespace

class DiagnosticsServiceTest : public QObject {
    Q_OBJECT
  private slots:
    void typedEventsPersistAndExport() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        choscordb::DiagnosticsService service(root.path(), "1.2.3", "test-build");
        QVERIFY(service.start());
        service.record({.event = choscordb::DiagnosticEvent::QueryFailed,
                        .driver = choscordb::DiagnosticDriver::PostgreSQL,
                        .errorClass = choscordb::DiagnosticErrorClass::Query,
                        .durationBucket = choscordb::DiagnosticDurationBucket::Under1s,
                        .openTabs = 3});
        service.flush();
        const auto summary = service.preview();
        QVERIFY(summary.hasHistory);
        QVERIFY(summary.categoryCounts.value("query_failed") == 1);
        const auto destination = root.filePath("report.zip");
        const auto result = service.exportZip(destination);
        QVERIFY2(result.success, qPrintable(result.error));
        QVERIFY(QFile::exists(destination));
        const auto events = zipMember(destination, "events.jsonl");
        QVERIFY(events.contains("\"event\":\"query_failed\""));
        QVERIFY(events.contains("\"driver\":\"postgresql\""));
        const auto manifest =
            QJsonDocument::fromJson(zipMember(destination, "manifest.json")).object();
        QCOMPARE(manifest.value("schema").toInt(), 1);
        QCOMPARE(manifest.value("app_version").toString(), QString("1.2.3"));
#if defined(__APPLE__)
        QCOMPARE(manifest.value("os_family").toString(), QString("macos"));
#elif defined(_WIN32)
        QCOMPARE(manifest.value("os_family").toString(), QString("windows"));
#elif defined(__linux__)
        QCOMPARE(manifest.value("os_family").toString(), QString("linux"));
#endif
        QCOMPARE(manifest.value("category_counts").toObject().value("query_failed").toInt(), 1);
        QCOMPARE(manifest.value("duration_bucket_counts").toObject().value("under_1_s").toInt(), 1);
        QVERIFY(manifest.value("unavailable_categories").toArray().contains("crash_signature"));
        service.stop();
    }

    void exportRejectsUnapprovedFileFieldsAndDoesNotIncludeUnrelatedData() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        choscordb::DiagnosticsService service(root.path(), "password=TOP_SECRET", "build/secret");
        QVERIFY(service.start());
        service.flush();
        const auto day = QDateTime::currentDateTimeUtc().date();
        const auto file =
            QDir(service.folderPath()).filePath(day.toString("yyyy-MM-dd") + ".jsonl");
        QFile append(file);
        QVERIFY(append.open(QIODevice::Append));
        append.write(fixtureLine(day, "query_failed", "SELECT TOP_SECRET FROM private_table"));
        append.close();
        QVERIFY(writeBytes(QDir(service.folderPath()).filePath("unrelated.txt"),
                           "PRIVATE_PROFILE_HOST_PATH"));
        service.record({.event = static_cast<choscordb::DiagnosticEvent>(999)});
        const auto destination = root.filePath("report.zip");
        QVERIFY(service.exportZip(destination).success);
        QFile archive(destination);
        QVERIFY(archive.open(QIODevice::ReadOnly));
        const auto bytes = archive.readAll();
        QVERIFY(!bytes.contains("TOP_SECRET"));
        QVERIFY(!bytes.contains("PRIVATE_PROFILE_HOST_PATH"));
        const auto manifest =
            QJsonDocument::fromJson(zipMember(destination, "manifest.json")).object();
        QCOMPARE(manifest.value("app_version").toString(), QString("unknown"));
        QVERIFY(manifest.value("invalid_records_omitted").toInt() >= 1);
        service.stop();
    }

    void exportRejectsMemoryFieldsOnNonMemoryEvents() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        service.flush();
        const auto day = QDateTime::currentDateTimeUtc().date();
        auto object = QJsonDocument::fromJson(fixtureLine(day, "query_failed")).object();
        object.insert("resident_bytes", 123456789);
        const auto path =
            QDir(service.folderPath()).filePath(day.toString("yyyy-MM-dd") + ".jsonl");
        QFile append(path);
        QVERIFY(append.open(QIODevice::Append));
        append.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
        append.close();
        const auto destination = root.filePath("report.zip");
        QVERIFY(service.exportZip(destination).success);
        const auto manifest =
            QJsonDocument::fromJson(zipMember(destination, "manifest.json")).object();
        QCOMPARE(manifest.value("category_counts").toObject().value("query_failed").toInt(), 0);
        QVERIFY(manifest.value("invalid_records_omitted").toInt() >= 1);
        service.stop();
    }

    void clearAndCancellationPreserveSelectedFileAndUnrelatedData() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        service.record({.event = choscordb::DiagnosticEvent::Error});
        service.flush();
        const auto userData = root.filePath("profiles.sqlite");
        QVERIFY(writeBytes(userData, "KEEP_USER_DATA"));
        const auto destination = root.filePath("existing.zip");
        QVERIFY(writeBytes(destination, "KEEP_EXISTING_ZIP"));
        std::atomic_bool cancelled = true;
        const auto result = service.exportZip(destination, &cancelled);
        QVERIFY(result.cancelled);
        QFile existing(destination);
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("KEEP_EXISTING_ZIP"));
        QVERIFY(service.clear());
        QCOMPARE(QDir(service.folderPath()).entryList({"run-*.marker"}, QDir::Files).size(), 1);
        QCOMPARE(service.preview().hasHistory, false);
        QVERIFY(service.exportZip(root.filePath("empty.zip")).success);
        const auto manifest =
            QJsonDocument::fromJson(zipMember(root.filePath("empty.zip"), "manifest.json"))
                .object();
        QCOMPARE(manifest.value("has_history").toBool(), false);
        QVERIFY(QFile::exists(userData));
        service.record({.event = choscordb::DiagnosticEvent::Error});
        QCOMPARE(service.preview().categoryCounts.value("error"), 1);
        service.stop();
    }

    void failedDestinationLeavesNoPartialZipAndReportsFailure() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        const auto destination = root.filePath("does-not-exist/report.zip");
        const auto result = service.exportZip(destination);
        QVERIFY(!result.success);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!QFile::exists(destination));
        QCOMPARE(service.preview().categoryCounts.value("export_failed"), 1);
        service.stop();
    }

    void failedLocalAppendIsCountedAndExported() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        service.flush();
        const auto day = QDateTime::currentDateTimeUtc().date();
        const auto artifact =
            QDir(service.folderPath()).filePath(day.toString("yyyy-MM-dd") + ".jsonl");
        QVERIFY(QFile::remove(artifact));
        QVERIFY(QDir().mkdir(artifact));
        service.record({.event = choscordb::DiagnosticEvent::QueryFailed});
        service.flush();
        QCOMPARE(service.preview().droppedRecords, quint64(1));
        QVERIFY(!service.warning().isEmpty());
        const auto destination = root.filePath("failed-write.zip");
        QVERIFY(service.exportZip(destination).success);
        const auto manifest =
            QJsonDocument::fromJson(zipMember(destination, "manifest.json")).object();
        QCOMPARE(manifest.value("dropped_records").toInt(), 1);
        service.stop();
    }

    void oldRecordsArePrunedAndOnlyLastSevenDaysAreExported() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        service.flush();
        const auto today = QDateTime::currentDateTimeUtc().date();
        const auto path = [&service](const QDate& day) {
            return QDir(service.folderPath()).filePath(day.toString("yyyy-MM-dd") + ".jsonl");
        };
        QVERIFY(
            writeBytes(path(today.addDays(-6)), fixtureLine(today.addDays(-6), "query_failed")));
        QVERIFY(writeBytes(path(today.addDays(-8)),
                           fixtureLine(today.addDays(-8), "connection_failed")));
        QVERIFY(writeBytes(path(today.addDays(-14)), fixtureLine(today.addDays(-14), "error")));
        QVERIFY(writeBytes(path(today.addDays(1)), fixtureLine(today.addDays(1), "error")));
        service.record({.event = choscordb::DiagnosticEvent::ResultPage});
        service.flush();
        QVERIFY(!QFile::exists(path(today.addDays(-14))));
        QVERIFY(!QFile::exists(path(today.addDays(1))));
        const auto summary = service.preview();
        QCOMPARE(summary.categoryCounts.value("query_failed"), 1);
        QCOMPARE(summary.categoryCounts.value("connection_failed"), 0);
        QVERIFY(QFile::exists(path(today.addDays(-8))));
        service.stop();
    }

    void dayCapKeepsRecentFailureAndReportsDropsAcrossLaunches() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        quint64 dropped = 0;
        {
            choscordb::DiagnosticsService service(root.path(), "1.2.3");
            QVERIFY(service.start());
            service.flush();
            const auto day = QDateTime::currentDateTimeUtc().date();
            const auto file =
                QDir(service.folderPath()).filePath(day.toString("yyyy-MM-dd") + ".jsonl");
            QByteArray oversized;
            while (oversized.size() < 270 * 1024)
                oversized += fixtureLine(day, "result_page");
            QVERIFY(writeBytes(file, oversized));
            service.record({.event = choscordb::DiagnosticEvent::QueryFailed,
                            .errorClass = choscordb::DiagnosticErrorClass::Query});
            service.flush();
            QVERIFY(QFileInfo(file).size() <= 256 * 1024);
            const auto summary = service.preview();
            QCOMPARE(summary.categoryCounts.value("query_failed"), 1);
            QVERIFY(summary.droppedRecords > 0);
            dropped = summary.droppedRecords;
            service.stop();
        }
        {
            choscordb::DiagnosticsService service(root.path(), "1.2.3");
            QVERIFY(service.start());
            QVERIFY(service.preview().droppedRecords >= dropped);
            service.stop();
        }
    }

    void staleMarkerBecomesUncleanExitButCleanStopDoesNot() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        {
            choscordb::DiagnosticsService service(root.path(), "1.2.3");
            QVERIFY(service.start());
            service.stop();
            QVERIFY(QDir(service.folderPath()).entryList({"run-*.marker"}, QDir::Files).isEmpty());
        }
        {
            choscordb::DiagnosticsService service(root.path(), "1.2.3");
            QVERIFY(service.start());
            QCOMPARE(service.preview().categoryCounts.value("unclean_exit"), 0);
            service.stop();
        }
        const auto marker =
            QDir(root.path())
                .filePath("diagnostics/run-999999999-00000000-0000-0000-0000-000000000000.marker");
        QVERIFY(writeBytes(marker, "running\n"));
        {
            choscordb::DiagnosticsService service(root.path(), "1.2.3");
            QVERIFY(service.start());
            QCOMPARE(service.preview().categoryCounts.value("unclean_exit"), 1);
            service.stop();
        }
    }

    void oldMarkerWithReusedLivePidIsPrunedWithoutCrashClaim() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto folder = QDir(root.path()).filePath("diagnostics");
        QVERIFY(QDir().mkpath(folder));
        const auto marker =
            QDir(folder).filePath(QString("run-%1-00000000-0000-0000-0000-000000000000.marker")
                                      .arg(QCoreApplication::applicationPid()));
        QFile old(marker);
        QVERIFY(old.open(QIODevice::ReadWrite));
        QCOMPARE(old.write("running\n"), qint64(8));
        QVERIFY(old.flush());
        QVERIFY(old.setFileTime(QDateTime::currentDateTimeUtc().addDays(-15),
                                QFileDevice::FileModificationTime));
        old.close();
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        QVERIFY(!QFile::exists(marker));
        QCOMPARE(service.preview().categoryCounts.value("unclean_exit"), 0);
        service.stop();
    }

    void staleMarkerFloodIsBoundedAndOmissionsAreCounted() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto folder = QDir(root.path()).filePath("diagnostics");
        QVERIFY(QDir().mkpath(folder));
        for (int index = 0; index < 100; ++index) {
            const auto marker =
                QDir(folder).filePath(QString("run-999999999-%1-0000-0000-0000-000000000000.marker")
                                          .arg(index, 8, 16, QLatin1Char('0')));
            QVERIFY(writeBytes(marker, "running\n"));
        }
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        const auto summary = service.preview();
        QVERIFY(summary.categoryCounts.value("unclean_exit") <= 16);
        QVERIFY(summary.droppedRecords >= 84);
        service.stop();
    }

    void liveConcurrentRunIsNotUncleanAndForcedChildIs() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--hold-diagnostics", root.path()});
        QVERIFY2(child.waitForStarted(10000), qPrintable(child.errorString()));
        QVERIFY2(child.waitForReadyRead(10000), qPrintable(child.errorString()));
        QVERIFY(child.readAllStandardOutput().contains("ready"));
        const auto folder = QDir(root.path()).filePath("diagnostics");
        {
            choscordb::DiagnosticsService service(root.path(), "1.2.3");
            QVERIFY(service.start());
            QCOMPARE(service.preview().categoryCounts.value("unclean_exit"), 0);
            QCOMPARE(QDir(folder).entryList({"run-*.marker"}, QDir::Files).size(), 2);
            service.stop();
            QCOMPARE(QDir(folder).entryList({"run-*.marker"}, QDir::Files).size(), 1);
        }
        child.kill();
        QVERIFY(child.waitForFinished(10000));
        {
            choscordb::DiagnosticsService service(root.path(), "1.2.3");
            QVERIFY(service.start());
            QCOMPARE(service.preview().categoryCounts.value("unclean_exit"), 1);
            service.stop();
        }
    }

    void deliberateCrashYieldsOnlyUncleanEvidence() {
#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--crash-diagnostics", root.path()});
        QVERIFY2(child.waitForStarted(10000), qPrintable(child.errorString()));
        const bool finished = child.waitForFinished(10000);
        if (!finished) {
            child.kill();
            child.waitForFinished(2000);
        }
        QVERIFY(finished);
        QVERIFY(child.readAllStandardOutput().contains("ready"));
        QCOMPARE(child.exitStatus(), QProcess::CrashExit);
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        QCOMPARE(service.preview().categoryCounts.value("unclean_exit"), 1);
        const auto destination = root.filePath("abort.zip");
        QVERIFY(service.exportZip(destination).success);
        const auto manifest =
            QJsonDocument::fromJson(zipMember(destination, "manifest.json")).object();
        QVERIFY(manifest.value("unavailable_categories").toArray().contains("crash_signature"));
        const auto events = zipMember(destination, "events.jsonl");
        QVERIFY(events.contains("\"event\":\"unclean_exit\""));
        QVERIFY(!events.contains("\"crash_signature\""));
        service.stop();
#else
        QSKIP("Deliberate crash is not available on this platform.");
#endif
    }

    void secondProcessLockDelaysAWriteAndLeavesNoIdentityData() {
        using namespace std::chrono_literals;
        QTemporaryDir root;
        QVERIFY(root.isValid());
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        service.flush();
        QProcess holder;
        holder.start(QCoreApplication::applicationFilePath(),
                     {"--hold-io-lock", service.folderPath()});
        QVERIFY2(holder.waitForStarted(10000), qPrintable(holder.errorString()));
        QVERIFY2(holder.waitForReadyRead(10000), qPrintable(holder.errorString()));
        QVERIFY(holder.readAllStandardOutput().contains("ready"));
        service.record({.event = choscordb::DiagnosticEvent::QueryFailed});
        auto drained = std::async(std::launch::async, [&service] { service.flush(); });
        const bool blocked = drained.wait_for(250ms) == std::future_status::timeout;
        holder.write("x\n");
        QVERIFY(holder.waitForFinished(10000));
        QVERIFY(drained.wait_for(10000ms) == std::future_status::ready);
        QVERIFY(blocked);
        QCOMPARE(service.preview().categoryCounts.value("query_failed"), 1);
        QFile lockFile(QDir(service.folderPath()).filePath(".io.lock"));
        QVERIFY(lockFile.open(QIODevice::ReadOnly));
        QCOMPARE(lockFile.readAll(), QByteArray{});
        service.stop();
    }

    void heldProcessLockBoundsBacklogDrainAndCountsEveryDrop() {
        using namespace std::chrono_literals;
        QTemporaryDir root;
        QVERIFY(root.isValid());
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        service.flush();
        QProcess holder;
        holder.start(QCoreApplication::applicationFilePath(),
                     {"--hold-io-lock", service.folderPath()});
        QVERIFY2(holder.waitForStarted(10000), qPrintable(holder.errorString()));
        QVERIFY2(holder.waitForReadyRead(10000), qPrintable(holder.errorString()));
        QVERIFY(holder.readAllStandardOutput().contains("ready"));
        for (int i = 0; i < 40; ++i)
            service.record({.event = choscordb::DiagnosticEvent::ResultPage});
        service.record({.event = choscordb::DiagnosticEvent::Error});
        auto drained = std::async(std::launch::async, [&service] { service.flush(); });
        const bool bounded = drained.wait_for(6s) == std::future_status::ready;
        holder.write("x");
        QVERIFY(holder.waitForFinished(10000));
        QVERIFY(drained.wait_for(10000ms) == std::future_status::ready);
        QVERIFY(bounded);
        QCOMPARE(service.preview().droppedRecords, quint64(41));
        service.stop();
    }

    void concurrentProcessesMergeDroppedRecordCounts() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        service.flush();
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--drop-after-signal", root.path()});
        QVERIFY2(child.waitForStarted(10000), qPrintable(child.errorString()));
        QVERIFY2(child.waitForReadyRead(10000), qPrintable(child.errorString()));
        QVERIFY(child.readAllStandardOutput().contains("ready"));
        const auto day = QDateTime::currentDateTimeUtc().date();
        const auto artifact =
            QDir(service.folderPath()).filePath(day.toString("yyyy-MM-dd") + ".jsonl");
        QVERIFY(QFile::remove(artifact));
        QVERIFY(QDir().mkdir(artifact));
        service.record({.event = choscordb::DiagnosticEvent::QueryFailed});
        service.flush();
        child.write("x");
        QVERIFY2(child.waitForReadyRead(10000), qPrintable(child.errorString()));
        QVERIFY(child.readAllStandardOutput().contains("done"));
        QCOMPARE(service.preview().droppedRecords, quint64(2));
        QVERIFY(QDir().rmdir(artifact));
        child.write("x");
        QVERIFY(child.waitForFinished(10000));
        service.stop();
    }

    void watchdogRecordsMainThreadStallAndRecovery() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        QObject mainContext;
        choscordb::DiagnosticsWatchdog watchdog(&mainContext, &service);
        watchdog.start();
        QTest::qWait(700);
        QCOMPARE(service.preview().categoryCounts.value("ui_hang_start"), 0);
        QTest::qSleep(2400);
        QTest::qWait(500);
        QCOMPARE(service.preview().categoryCounts.value("ui_hang_start"), 1);
        QCOMPARE(service.preview().categoryCounts.value("ui_hang_end"), 1);
        const auto destination = root.filePath("hang.zip");
        QVERIFY(service.exportZip(destination).success);
        const auto events = zipMember(destination, "events.jsonl").split('\n');
        bool measured = false;
        for (const auto& line : events) {
            const auto object = QJsonDocument::fromJson(line).object();
            if (object.value("event") == QLatin1String("ui_hang_end"))
                measured = object.value("duration_ms").toInt() >= 2000;
        }
        QVERIFY(measured);
        watchdog.stop();
        service.stop();
    }

    void forcedMemorySampleShowsBoundedGrowth() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        choscordb::DiagnosticsService service(root.path(), "1.2.3");
        QVERIFY(service.start());
        service.sampleMemory(1);
        service.flush();
        QByteArray allocation(32 * 1024 * 1024, char(0x5a));
        QVERIFY(allocation.size() == 32 * 1024 * 1024);
        QTest::qSleep(5200);
        service.sampleMemory(2, true);
        service.sampleMemory(3, true);
        service.flush();
        const auto destination = root.filePath("memory.zip");
        QVERIFY(service.exportZip(destination).success);
        QList<QJsonObject> samples;
        for (const auto& line : zipMember(destination, "events.jsonl").split('\n')) {
            const auto object = QJsonDocument::fromJson(line).object();
            if (object.value("event") == QLatin1String("memory_sample"))
                samples.append(object);
        }
        QCOMPARE(samples.size(), 2);
        QCOMPARE(samples.at(0).value("open_tabs").toInt(), 1);
        QCOMPARE(samples.at(1).value("open_tabs").toInt(), 2);
        QVERIFY(samples.at(1).value("resident_bytes").toDouble() >=
                samples.at(0).value("resident_bytes").toDouble() + 16 * 1024 * 1024);
        service.stop();
    }
};

int main(int argc, char** argv) {
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--hold-diagnostics")) {
        QCoreApplication app(argc, argv);
        choscordb::DiagnosticsService service(QString::fromLocal8Bit(argv[2]), "1.2.3");
        if (!service.start())
            return 2;
        std::fputs("ready\n", stdout);
        std::fflush(stdout);
        char command = 0;
        std::cin.get(command);
        service.stop();
        return 0;
    }
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--hold-io-lock")) {
        QCoreApplication app(argc, argv);
        choscordb::DiagnosticsFileLock lock(QString::fromLocal8Bit(argv[2]));
        if (!lock.lock(5000))
            return 2;
        std::fputs("ready\n", stdout);
        std::fflush(stdout);
        char command = 0;
        std::cin.get(command);
        lock.unlock();
        return 0;
    }
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--drop-after-signal")) {
        QCoreApplication app(argc, argv);
        choscordb::DiagnosticsService service(QString::fromLocal8Bit(argv[2]), "1.2.3");
        if (!service.start())
            return 2;
        service.flush();
        std::fputs("ready\n", stdout);
        std::fflush(stdout);
        char command = 0;
        std::cin.get(command);
        service.record({.event = choscordb::DiagnosticEvent::QueryFailed});
        service.flush();
        std::fputs("done\n", stdout);
        std::fflush(stdout);
        std::cin.get(command);
        service.stop();
        return 0;
    }
#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--crash-diagnostics")) {
        QCoreApplication app(argc, argv);
#if defined(__APPLE__) || defined(__linux__)
        struct rlimit limit{0, 0};
        setrlimit(RLIMIT_CORE, &limit);
#else
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
#endif
        choscordb::DiagnosticsService service(QString::fromLocal8Bit(argv[2]), "1.2.3");
        if (!service.start())
            return 2;
        service.flush();
        std::fputs("ready\n", stdout);
        std::fflush(stdout);
#if defined(_WIN32)
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#endif
        std::abort();
    }
#endif
    QCoreApplication app(argc, argv);
    DiagnosticsServiceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "diagnostics_service_test.moc"
