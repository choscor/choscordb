#include "app/diagnostics_service.h"
#include "app/diagnostics_file_lock.h"
#include "app/diagnostics_memory.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>
#include <QTime>
#include <QTimeZone>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#if defined(__APPLE__)
#include <cerrno>
#include <csignal>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

namespace choscordb {
namespace {
constexpr int schemaVersion = 1;
constexpr int retentionDays = 14;
constexpr int exportDays = 7;
constexpr qint64 maxDayBytes = 256 * 1024;
constexpr qint64 maxExportBytes = 2 * 1024 * 1024;
constexpr std::size_t maxQueuedRecords = 1024;
constexpr qint64 memoryIntervalMs = 60 * 1000;

struct Pending {
    DiagnosticRecord record;
    QDateTime utc;
};

QString safeVersion(QString value) {
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9._+\\-]{1,40}$"));
    return allowed.match(value).hasMatch() ? value : QStringLiteral("unknown");
}
QString osFamily() {
#if defined(__APPLE__)
    return QStringLiteral("macos");
#elif defined(_WIN32)
    return QStringLiteral("windows");
#elif defined(__linux__)
    return QStringLiteral("linux");
#else
    return QStringLiteral("unknown");
#endif
}
QString eventName(DiagnosticEvent event) {
    switch (event) {
    case DiagnosticEvent::Startup:
        return QStringLiteral("startup");
    case DiagnosticEvent::Shutdown:
        return QStringLiteral("shutdown");
    case DiagnosticEvent::ConnectionSucceeded:
        return QStringLiteral("connection_succeeded");
    case DiagnosticEvent::ConnectionFailed:
        return QStringLiteral("connection_failed");
    case DiagnosticEvent::QuerySucceeded:
        return QStringLiteral("query_succeeded");
    case DiagnosticEvent::QueryFailed:
        return QStringLiteral("query_failed");
    case DiagnosticEvent::ResultPage:
        return QStringLiteral("result_page");
    case DiagnosticEvent::ExportSucceeded:
        return QStringLiteral("export_succeeded");
    case DiagnosticEvent::ExportFailed:
        return QStringLiteral("export_failed");
    case DiagnosticEvent::Cancelled:
        return QStringLiteral("cancelled");
    case DiagnosticEvent::UiHangStart:
        return QStringLiteral("ui_hang_start");
    case DiagnosticEvent::UiHangEnd:
        return QStringLiteral("ui_hang_end");
    case DiagnosticEvent::UncleanExit:
        return QStringLiteral("unclean_exit");
    case DiagnosticEvent::Error:
        return QStringLiteral("error");
    case DiagnosticEvent::MemorySample:
        return QStringLiteral("memory_sample");
    }
    return {};
}
QString driverName(DiagnosticDriver driver) {
    switch (driver) {
    case DiagnosticDriver::Unknown:
        return QStringLiteral("unknown");
    case DiagnosticDriver::SQLite:
        return QStringLiteral("sqlite");
    case DiagnosticDriver::PostgreSQL:
        return QStringLiteral("postgresql");
    case DiagnosticDriver::MySQL:
        return QStringLiteral("mysql");
    }
    return {};
}
QString errorName(DiagnosticErrorClass error) {
    switch (error) {
    case DiagnosticErrorClass::Unknown:
        return QStringLiteral("unknown");
    case DiagnosticErrorClass::Connection:
        return QStringLiteral("connection");
    case DiagnosticErrorClass::Authentication:
        return QStringLiteral("authentication");
    case DiagnosticErrorClass::Query:
        return QStringLiteral("query");
    case DiagnosticErrorClass::IO:
        return QStringLiteral("io");
    case DiagnosticErrorClass::Internal:
        return QStringLiteral("internal");
    }
    return {};
}
QString bucketName(DiagnosticDurationBucket bucket) {
    switch (bucket) {
    case DiagnosticDurationBucket::Unknown:
        return QStringLiteral("unknown");
    case DiagnosticDurationBucket::Under100Ms:
        return QStringLiteral("under_100_ms");
    case DiagnosticDurationBucket::Under1s:
        return QStringLiteral("under_1_s");
    case DiagnosticDurationBucket::Under10s:
        return QStringLiteral("under_10_s");
    case DiagnosticDurationBucket::Over10s:
        return QStringLiteral("over_10_s");
    }
    return {};
}
bool highValue(DiagnosticEvent event) {
    switch (event) {
    case DiagnosticEvent::ConnectionFailed:
    case DiagnosticEvent::QueryFailed:
    case DiagnosticEvent::UiHangStart:
    case DiagnosticEvent::UiHangEnd:
    case DiagnosticEvent::UncleanExit:
    case DiagnosticEvent::Error:
    case DiagnosticEvent::ExportFailed:
        return true;
    default:
        return false;
    }
}
QJsonObject serialized(const Pending& pending) {
    const auto& record = pending.record;
    QJsonObject object{{QStringLiteral("schema"), schemaVersion},
                       {QStringLiteral("utc"), pending.utc.toString(Qt::ISODateWithMs)},
                       {QStringLiteral("event"), eventName(record.event)},
                       {QStringLiteral("driver"), driverName(record.driver)},
                       {QStringLiteral("error_class"), errorName(record.errorClass)},
                       {QStringLiteral("duration_bucket"), bucketName(record.durationBucket)},
                       {QStringLiteral("open_tabs"), std::clamp(record.openTabs, 0, 1000)}};
    if (record.event == DiagnosticEvent::MemorySample) {
        if (const auto value = residentBytes())
            object.insert(QStringLiteral("resident_bytes"), double(*value));
        if (const auto value = footprintBytes())
            object.insert(QStringLiteral("footprint_bytes"), double(*value));
        if (const auto value = peakBytes())
            object.insert(QStringLiteral("peak_bytes"), double(*value));
    }
    if (record.event == DiagnosticEvent::UiHangEnd)
        object.insert(QStringLiteral("duration_ms"), std::clamp(record.durationMs, 0, 600000));
    return object;
}
bool validStoredRecord(const QJsonObject& object, QDateTime* utc) {
    if (object.value(QStringLiteral("schema")).toInt() != schemaVersion)
        return false;
    static const QSet<QString> keys = {
        QStringLiteral("schema"),          QStringLiteral("utc"),
        QStringLiteral("event"),           QStringLiteral("driver"),
        QStringLiteral("error_class"),     QStringLiteral("duration_bucket"),
        QStringLiteral("open_tabs"),       QStringLiteral("resident_bytes"),
        QStringLiteral("footprint_bytes"), QStringLiteral("peak_bytes"),
        QStringLiteral("duration_ms")};
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!keys.contains(it.key()))
            return false;
    }
    const auto known = [](const QString& value, auto mapping) {
        for (int i = 0; i < 32; ++i) {
            if (!mapping(i).isEmpty() && mapping(i) == value)
                return true;
        }
        return false;
    };
    if (!known(object.value(QStringLiteral("event")).toString(),
               [](int i) { return eventName(static_cast<DiagnosticEvent>(i)); }) ||
        !known(object.value(QStringLiteral("driver")).toString(),
               [](int i) { return driverName(static_cast<DiagnosticDriver>(i)); }) ||
        !known(object.value(QStringLiteral("error_class")).toString(),
               [](int i) { return errorName(static_cast<DiagnosticErrorClass>(i)); }) ||
        !known(object.value(QStringLiteral("duration_bucket")).toString(),
               [](int i) { return bucketName(static_cast<DiagnosticDurationBucket>(i)); }))
        return false;
    const auto tabs = object.value(QStringLiteral("open_tabs"));
    if (!tabs.isDouble() || tabs.toInt(-1) < 0 || tabs.toInt(-1) > 1000)
        return false;
    const auto duration = object.value(QStringLiteral("duration_ms"));
    if (!duration.isUndefined() &&
        (object.value(QStringLiteral("event")) != QStringLiteral("ui_hang_end") ||
         !duration.isDouble() || duration.toInt(-1) < 0 || duration.toInt(-1) > 600000))
        return false;
    const bool memory = object.value(QStringLiteral("event")) == QStringLiteral("memory_sample");
    for (const auto& key : {"resident_bytes", "footprint_bytes", "peak_bytes"}) {
        const auto value = object.value(QLatin1String(key));
        if (!value.isUndefined() &&
            (!memory || !value.isDouble() || value.toDouble() < 0 || value.toDouble() > 1.0e16))
            return false;
    }
    *utc = QDateTime::fromString(object.value(QStringLiteral("utc")).toString(), Qt::ISODateWithMs)
               .toUTC();
    return utc->isValid();
}
QByteArray jsonLine(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}
void append16(QByteArray& bytes, quint16 value) {
    bytes.append(char(value & 0xff));
    bytes.append(char((value >> 8) & 0xff));
}
void append32(QByteArray& bytes, quint32 value) {
    append16(bytes, quint16(value & 0xffff));
    append16(bytes, quint16(value >> 16));
}
quint32 crc32(const QByteArray& bytes) {
    quint32 crc = 0xffffffff;
    for (const auto byte : bytes) {
        crc ^= quint8(byte);
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
QByteArray zipEntries(const QList<QPair<QByteArray, QByteArray>>& entries) {
    QByteArray output, directory;
    for (const auto& [name, contents] : entries) {
        const quint32 offset = quint32(output.size());
        const quint32 size = quint32(contents.size());
        const quint32 crc = crc32(contents);
        append32(output, 0x04034b50);
        append16(output, 20);
        append16(output, 0);
        append16(output, 0);
        append16(output, 0);
        append16(output, 0);
        append32(output, crc);
        append32(output, size);
        append32(output, size);
        append16(output, quint16(name.size()));
        append16(output, 0);
        output += name;
        output += contents;
        append32(directory, 0x02014b50);
        append16(directory, 20);
        append16(directory, 20);
        append16(directory, 0);
        append16(directory, 0);
        append16(directory, 0);
        append16(directory, 0);
        append32(directory, crc);
        append32(directory, size);
        append32(directory, size);
        append16(directory, quint16(name.size()));
        append16(directory, 0);
        append16(directory, 0);
        append16(directory, 0);
        append16(directory, 0);
        append32(directory, 0);
        append32(directory, offset);
        directory += name;
    }
    const quint32 directoryOffset = quint32(output.size());
    output += directory;
    append32(output, 0x06054b50);
    append16(output, 0);
    append16(output, 0);
    append16(output, quint16(entries.size()));
    append16(output, quint16(entries.size()));
    append32(output, quint32(directory.size()));
    append32(output, directoryOffset);
    append16(output, 0);
    return output;
}
QString dayPath(const QString& folder, const QDate& day) {
    return QDir(folder).filePath(day.toString(QStringLiteral("yyyy-MM-dd")) + ".jsonl");
}
bool isDayFile(const QString& name) {
    static const QRegularExpression pattern(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}\\.jsonl$"));
    return pattern.match(name).hasMatch() &&
           QDate::fromString(name.left(10), QStringLiteral("yyyy-MM-dd")).isValid();
}
bool processAlive(qint64 pid) {
    if (pid <= 0)
        return false;
#if defined(_WIN32)
    if (pid > std::numeric_limits<DWORD>::max())
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!process)
        return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD status = 0;
    const bool alive = GetExitCodeProcess(process, &status) && status == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#elif defined(__APPLE__) || defined(__linux__)
    if (pid > std::numeric_limits<pid_t>::max())
        return false;
    return kill(pid_t(pid), 0) == 0 || errno == EPERM;
#else
    return true; // Unknown platform: avoid an unsupported crash claim.
#endif
}
int removeStaleMarkers(const QString& folder) {
    static const QRegularExpression pattern(
        QStringLiteral("^run-(\\d+)-[0-9a-fA-F-]{36}\\.marker$"));
    QDir directory(folder);
    int stale = 0;
    const auto oldest = QDateTime::currentDateTimeUtc().addDays(-retentionDays);
    for (const auto& name : directory.entryList(
             {QStringLiteral("run-*.marker")}, QDir::Files | QDir::System | QDir::NoDotAndDotDot)) {
        const auto match = pattern.match(name);
        if (!match.hasMatch())
            continue;
        bool ok = false;
        const auto pid = match.captured(1).toLongLong(&ok);
        const QFileInfo info(directory.filePath(name));
        if (!ok || info.isSymLink())
            continue;
        if (info.lastModified().toUTC() < oldest) {
            QFile::remove(info.filePath()); // Expired evidence makes no termination claim.
            continue;
        }
        if (processAlive(pid))
            continue;
        if (QFile::remove(directory.filePath(name)))
            ++stale;
    }
    return stale;
}
quint64 readDropped(const QString& folder) {
    const QFileInfo info(QDir(folder).filePath(QStringLiteral("stats.json")));
    if (!info.isFile() || info.isSymLink() || info.size() > 1024)
        return 0;
    QFile input(info.filePath());
    if (!input.open(QIODevice::ReadOnly))
        return 0;
    const auto object = QJsonDocument::fromJson(input.readAll()).object();
    const auto value = object.value(QStringLiteral("dropped_records")).toDouble(-1);
    if (object.value(QStringLiteral("schema")).toInt() != schemaVersion || value < 0 ||
        value > 1.0e15)
        return 0;
    return quint64(value);
}
bool writeDropped(const QString& folder, quint64 dropped) {
    const auto path = QDir(folder).filePath(QStringLiteral("stats.json"));
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
        return false;
    const auto bytes = jsonLine({{QStringLiteral("schema"), schemaVersion},
                                 {QStringLiteral("dropped_records"), double(dropped)}});
    if (output.write(bytes) != bytes.size() || !output.commit())
        return false;
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}
bool mergeDropped(const QString& folder, quint64 current, quint64 synced) {
    const quint64 delta = current >= synced ? current - synced : current;
    if (delta == 0)
        return true;
    const auto prior = readDropped(folder);
    return writeDropped(folder, prior + delta);
}
void touchMarker(const QString& path) {
    QFile marker(path);
    if (marker.open(QIODevice::ReadWrite))
        marker.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
}
bool appendBounded(const QString& path, const QByteArray& line, bool priority,
                   std::atomic<quint64>* dropped) {
    QFileInfo info(path);
    if (info.isSymLink() || line.size() > maxDayBytes) {
        return false;
    }
    if (info.exists() && info.size() > maxDayBytes) {
        // Never read a corrupt or externally enlarged artifact without a bound.
        QSaveFile replace(path);
        if (!replace.open(QIODevice::WriteOnly) || replace.write(line) != line.size() ||
            !replace.commit())
            return false;
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        ++*dropped;
        return true;
    }
    if (info.exists() && info.size() + line.size() > maxDayBytes) {
        QFile existing(path);
        if (!existing.open(QIODevice::ReadOnly)) {
            return false;
        }
        QList<QByteArray> lines;
        qint64 size = 0;
        const auto day = QDate::fromString(info.fileName().left(10), QStringLiteral("yyyy-MM-dd"));
        while (!existing.atEnd()) {
            const auto raw = existing.readLine();
            const auto document = QJsonDocument::fromJson(raw);
            QDateTime utc;
            if (!document.isObject() || !validStoredRecord(document.object(), &utc) ||
                utc.date() != day) {
                ++*dropped;
                continue;
            }
            const auto canonical = jsonLine(document.object());
            lines.append(canonical);
            size += canonical.size();
        }
        existing.close();
        while (size + line.size() > maxDayBytes && !lines.isEmpty()) {
            int remove = -1;
            for (int i = 0; i < lines.size(); ++i) {
                const auto event = QJsonDocument::fromJson(lines.at(i))
                                       .object()
                                       .value(QStringLiteral("event"))
                                       .toString();
                if (!QSet<QString>{QStringLiteral("connection_failed"),
                                   QStringLiteral("query_failed"), QStringLiteral("ui_hang_start"),
                                   QStringLiteral("ui_hang_end"), QStringLiteral("unclean_exit"),
                                   QStringLiteral("error"), QStringLiteral("export_failed")}
                         .contains(event)) {
                    remove = i;
                    break;
                }
            }
            if (remove < 0 && priority)
                remove = 0;
            if (remove < 0) {
                return false;
            }
            size -= lines.at(remove).size();
            lines.removeAt(remove);
            ++*dropped;
        }
        QSaveFile replace(path);
        if (!replace.open(QIODevice::WriteOnly))
            return false;
        for (const auto& kept : lines) {
            if (replace.write(kept) != kept.size())
                return false;
        }
        if (!replace.commit())
            return false;
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Append))
        return false;
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return output.write(line) == line.size() && output.flush();
}
void prune(const QString& folder) {
    const QDate today = QDateTime::currentDateTimeUtc().date();
    const QDate oldest = today.addDays(1 - retentionDays);
    QDir directory(folder);
    for (const auto& name :
         directory.entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot)) {
        if (!isDayFile(name))
            continue;
        const QFileInfo info(directory.filePath(name));
        const auto day = QDate::fromString(name.left(10), QStringLiteral("yyyy-MM-dd"));
        if (info.isSymLink() || day < oldest || day > today)
            QFile::remove(info.filePath());
    }
}
struct Snapshot {
    QByteArray events;
    DiagnosticSummary summary;
    quint64 invalidRecords = 0;
    quint64 omittedByCap = 0;
};
Snapshot snapshot(const QString& folder, quint64 dropped) {
    Snapshot result;
    result.summary.droppedRecords = dropped;
    result.summary.unavailableCategories = {QStringLiteral("crash_signature"),
                                            QStringLiteral("native_thread_sample")};
    bool residentAvailable = false, footprintAvailable = false, peakAvailable = false;
    const auto today = QDateTime::currentDateTimeUtc().date();
    for (int days = exportDays - 1; days >= 0; --days) {
        const auto path = dayPath(folder, today.addDays(-days));
        const QFileInfo info(path);
        if (!info.isFile() || info.isSymLink())
            continue;
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly))
            continue;
        qint64 read = 0;
        while (!input.atEnd() && read <= maxDayBytes) {
            const QByteArray line = input.readLine(maxDayBytes + 1);
            read += line.size();
            if (line.size() > 1024 || !line.endsWith('\n')) {
                ++result.invalidRecords;
                continue;
            }
            const auto document = QJsonDocument::fromJson(line);
            QDateTime utc;
            if (!document.isObject() || !validStoredRecord(document.object(), &utc) ||
                utc.date() != today.addDays(-days)) {
                ++result.invalidRecords;
                continue;
            }
            const auto canonical = jsonLine(document.object());
            if (result.events.size() + canonical.size() > maxExportBytes - 8192) {
                ++result.omittedByCap;
                continue;
            }
            result.events += canonical;
            const auto event = document.object().value(QStringLiteral("event")).toString();
            ++result.summary.categoryCounts[event];
            const auto bucket =
                document.object().value(QStringLiteral("duration_bucket")).toString();
            if (bucket != QStringLiteral("unknown"))
                ++result.summary.durationBucketCounts[bucket];
            if (event == QStringLiteral("memory_sample")) {
                residentAvailable |= document.object().contains(QStringLiteral("resident_bytes"));
                footprintAvailable |= document.object().contains(QStringLiteral("footprint_bytes"));
                peakAvailable |= document.object().contains(QStringLiteral("peak_bytes"));
            }
            if (!result.summary.fromUtc.isValid() || utc < result.summary.fromUtc)
                result.summary.fromUtc = utc;
            if (!result.summary.toUtc.isValid() || utc > result.summary.toUtc)
                result.summary.toUtc = utc;
        }
        if (!input.atEnd() || read > maxDayBytes)
            ++result.invalidRecords;
    }
    if (!residentAvailable)
        result.summary.unavailableCategories.append(QStringLiteral("resident_memory"));
    if (!footprintAvailable)
        result.summary.unavailableCategories.append(QStringLiteral("physical_footprint"));
    if (!peakAvailable)
        result.summary.unavailableCategories.append(QStringLiteral("peak_memory"));
    result.summary.hasHistory = !result.events.isEmpty();
    result.summary.estimatedBytes = result.events.size() + 2048;
    return result;
}
} // namespace

struct DiagnosticsService::State {
    QString folder;
    QString markerPath;
    QString version;
    QString build;
    std::mutex queueMutex;
    std::mutex ioMutex;
    std::condition_variable pendingChanged;
    std::condition_variable idle;
    std::deque<Pending> queue;
    std::thread writer;
    bool running = false;
    bool stopping = false;
    bool writing = false;
    std::atomic<quint64> dropped = 0;
    std::atomic<quint64> syncedDropped = 0;
    QString lastWarning;
    QDateTime lastMemory;
};

DiagnosticsService::DiagnosticsService(QString applicationDataDirectory, QString appVersion,
                                       QString buildVersion)
    : state_(std::make_unique<State>()) {
    state_->folder = QDir(applicationDataDirectory).filePath(QStringLiteral("diagnostics"));
    state_->version = safeVersion(std::move(appVersion));
    state_->build = safeVersion(std::move(buildVersion));
}
DiagnosticsService::~DiagnosticsService() {
    stop();
}

bool DiagnosticsService::start() {
    std::lock_guard guard(state_->queueMutex);
    if (state_->running)
        return true;
    if (QFileInfo(state_->folder).isSymLink()) {
        state_->lastWarning = QStringLiteral("Diagnostics storage unavailable.");
        return false;
    }
    QDir directory;
    if (!directory.mkpath(state_->folder) || QFileInfo(state_->folder).isSymLink()) {
        state_->lastWarning = QStringLiteral("Diagnostics storage unavailable.");
        return false;
    }
    QFile::setPermissions(state_->folder,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    int staleMarkers = 0;
    {
        DiagnosticsFileLock fileLock(state_->folder);
        if (!fileLock.lock(3000)) {
            state_->lastWarning = QStringLiteral("Diagnostics storage unavailable.");
            return false;
        }
        staleMarkers = removeStaleMarkers(state_->folder);
        state_->markerPath =
            QDir(state_->folder)
                .filePath(QStringLiteral("run-%1-%2.marker")
                              .arg(QCoreApplication::applicationPid())
                              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QSaveFile running(state_->markerPath);
        if (!running.open(QIODevice::WriteOnly) || running.write("running\n") != 8 ||
            !running.commit()) {
            state_->lastWarning = QStringLiteral("Diagnostics run marker unavailable.");
            return false;
        }
        QFile::setPermissions(state_->markerPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        state_->dropped = readDropped(state_->folder);
        state_->syncedDropped = state_->dropped.load();
    }
    state_->stopping = false;
    state_->running = true;
    try {
        state_->writer = std::thread([this] {
            for (;;) {
                Pending pending;
                {
                    std::unique_lock lock(state_->queueMutex);
                    state_->pendingChanged.wait(
                        lock, [this] { return state_->stopping || !state_->queue.empty(); });
                    if (state_->queue.empty() && state_->stopping)
                        break;
                    pending = std::move(state_->queue.front());
                    state_->queue.pop_front();
                    state_->writing = true;
                }
                bool ok = true;
                bool statsOk = true;
                {
                    std::lock_guard io(state_->ioMutex);
                    const auto persistDrops = [this, &statsOk] {
                        const auto current = state_->dropped.load();
                        const auto synced = state_->syncedDropped.load();
                        if (current != synced) {
                            statsOk = mergeDropped(state_->folder, current, synced);
                            if (statsOk)
                                state_->syncedDropped = current;
                        }
                        touchMarker(state_->markerPath);
                    };
                    DiagnosticsFileLock fileLock(state_->folder);
                    if (!fileLock.lock(3000)) {
                        ok = false;
                        std::deque<Pending> backlog;
                        {
                            std::lock_guard queueLock(state_->queueMutex);
                            backlog.swap(state_->queue);
                        }
                        backlog.push_front(std::move(pending));
                        std::deque<Pending> priority;
                        for (auto& item : backlog) {
                            if (highValue(item.record.event) && priority.size() < 8)
                                priority.push_back(std::move(item));
                            else
                                ++state_->dropped;
                        }
                        DiagnosticsFileLock retry(state_->folder);
                        if (!priority.empty() && retry.lock(500)) {
                            for (auto& item : priority) {
                                const auto line = jsonLine(serialized(item));
                                if (!appendBounded(dayPath(state_->folder, item.utc.date()), line,
                                                   true, &state_->dropped))
                                    ++state_->dropped;
                            }
                            prune(state_->folder);
                            persistDrops();
                        } else {
                            state_->dropped += priority.size();
                        }
                    } else {
                        if (pending.record.event == DiagnosticEvent::MemorySample)
                            pending.utc = QDateTime::currentDateTimeUtc();
                        const auto memoryInterval =
                            pending.record.forceMemory ? 5000 : memoryIntervalMs;
                        if (pending.record.event == DiagnosticEvent::MemorySample &&
                            state_->lastMemory.isValid() &&
                            state_->lastMemory.msecsTo(pending.utc) >= 0 &&
                            state_->lastMemory.msecsTo(pending.utc) < memoryInterval) {
                            // One sample per minute is enough for routine trends.
                        } else {
                            if (pending.record.event == DiagnosticEvent::MemorySample)
                                state_->lastMemory = pending.utc;
                            const auto line = jsonLine(serialized(pending));
                            ok = appendBounded(dayPath(state_->folder, pending.utc.date()), line,
                                               highValue(pending.record.event), &state_->dropped);
                            prune(state_->folder);
                        }
                        if (!ok)
                            ++state_->dropped;
                        persistDrops();
                    }
                }
                {
                    std::lock_guard lock(state_->queueMutex);
                    if (!ok || !statsOk)
                        state_->lastWarning =
                            QStringLiteral("Some diagnostics could not be saved.");
                    state_->writing = false;
                }
                state_->idle.notify_all();
            }
            state_->idle.notify_all();
        });
    } catch (...) {
        state_->running = false;
        QFile::remove(state_->markerPath);
        state_->markerPath.clear();
        state_->lastWarning = QStringLiteral("Diagnostics writer unavailable.");
        return false;
    }
    const int reportedStaleMarkers = std::min(staleMarkers, 16);
    state_->dropped += staleMarkers - reportedStaleMarkers;
    for (int i = 0; i < reportedStaleMarkers; ++i)
        state_->queue.push_back(
            {{.event = DiagnosticEvent::UncleanExit}, QDateTime::currentDateTimeUtc()});
    state_->queue.push_back({{.event = DiagnosticEvent::Startup}, QDateTime::currentDateTimeUtc()});
    state_->pendingChanged.notify_one();
    return true;
}
void DiagnosticsService::stop() {
    {
        std::lock_guard lock(state_->queueMutex);
        if (!state_->running)
            return;
        state_->queue.push_back(
            {{.event = DiagnosticEvent::Shutdown}, QDateTime::currentDateTimeUtc()});
        state_->stopping = true;
        state_->pendingChanged.notify_one();
    }
    if (state_->writer.joinable())
        state_->writer.join();
    {
        std::lock_guard io(state_->ioMutex);
        DiagnosticsFileLock fileLock(state_->folder);
        if (!fileLock.lock(3000) ||
            !mergeDropped(state_->folder, state_->dropped, state_->syncedDropped)) {
            std::lock_guard lock(state_->queueMutex);
            state_->lastWarning = QStringLiteral("Some diagnostics could not be saved.");
        } else {
            state_->syncedDropped = state_->dropped.load();
        }
    }
    QFile::remove(state_->markerPath);
    state_->markerPath.clear();
    std::lock_guard lock(state_->queueMutex);
    state_->running = false;
}
void DiagnosticsService::record(DiagnosticRecord record) noexcept {
    try {
        if (eventName(record.event).isEmpty() || driverName(record.driver).isEmpty() ||
            errorName(record.errorClass).isEmpty() || bucketName(record.durationBucket).isEmpty())
            return;
        std::lock_guard lock(state_->queueMutex);
        if (!state_->running || state_->stopping)
            return;
        if (state_->queue.size() >= maxQueuedRecords) {
            if (highValue(record.event)) {
                const auto routine = std::find_if(
                    state_->queue.begin(), state_->queue.end(),
                    [](const Pending& pending) { return !highValue(pending.record.event); });
                if (routine != state_->queue.end())
                    state_->queue.erase(routine);
                else
                    state_->queue.pop_front();
            } else {
                ++state_->dropped;
                return;
            }
            ++state_->dropped;
        }
        state_->queue.push_back({record, QDateTime::currentDateTimeUtc()});
        state_->pendingChanged.notify_one();
    } catch (...) {
        ++state_->dropped;
    }
}
void DiagnosticsService::sampleMemory(int openTabs, bool force) noexcept {
    record({.event = DiagnosticEvent::MemorySample, .openTabs = openTabs, .forceMemory = force});
}
void DiagnosticsService::flush() {
    std::unique_lock lock(state_->queueMutex);
    state_->idle.wait(lock, [this] { return state_->queue.empty() && !state_->writing; });
}
DiagnosticSummary DiagnosticsService::preview() {
    flush();
    std::lock_guard io(state_->ioMutex);
    DiagnosticsFileLock fileLock(state_->folder);
    if (!fileLock.lock(500)) {
        DiagnosticSummary unavailable;
        unavailable.unavailableCategories.append(QStringLiteral("diagnostic_snapshot"));
        return unavailable;
    }
    const auto current = state_->dropped.load(), synced = state_->syncedDropped.load();
    const auto unsynced = current >= synced ? current - synced : current;
    return snapshot(state_->folder, readDropped(state_->folder) + unsynced).summary;
}
DiagnosticExportResult DiagnosticsService::exportZip(const QString& destination,
                                                     const std::atomic_bool* cancel) {
    const auto cancelled = [this] {
        record({.event = DiagnosticEvent::Cancelled});
        return DiagnosticExportResult{.cancelled = true};
    };
    const auto failed = [this](QString message) {
        record({.event = DiagnosticEvent::ExportFailed, .errorClass = DiagnosticErrorClass::IO});
        return DiagnosticExportResult{.error = std::move(message)};
    };
    if (cancel && cancel->load())
        return cancelled();
    flush();
    Snapshot data;
    {
        std::lock_guard io(state_->ioMutex);
        DiagnosticsFileLock fileLock(state_->folder);
        if (!fileLock.lock(3000))
            return failed(QStringLiteral("Could not read a consistent diagnostics snapshot."));
        const auto current = state_->dropped.load(), synced = state_->syncedDropped.load();
        const auto unsynced = current >= synced ? current - synced : current;
        data = snapshot(state_->folder, readDropped(state_->folder) + unsynced);
    }
    if (cancel && cancel->load())
        return cancelled();
    QJsonObject counts;
    for (auto it = data.summary.categoryCounts.begin(); it != data.summary.categoryCounts.end();
         ++it)
        counts.insert(it.key(), it.value());
    QJsonObject bucketCounts;
    for (auto it = data.summary.durationBucketCounts.begin();
         it != data.summary.durationBucketCounts.end(); ++it)
        bucketCounts.insert(it.key(), it.value());
    QJsonArray unavailable;
    for (const auto& category : data.summary.unavailableCategories)
        unavailable.append(category);
    const auto now = QDateTime::currentDateTimeUtc();
    QJsonObject manifest{
        {QStringLiteral("schema"), schemaVersion},
        {QStringLiteral("app_version"), state_->version},
        {QStringLiteral("build_version"), state_->build},
        {QStringLiteral("os_family"), osFamily()},
        {QStringLiteral("os_version"), safeVersion(QSysInfo::productVersion())},
        {QStringLiteral("architecture"), safeVersion(QSysInfo::currentCpuArchitecture())},
        {QStringLiteral("coverage_start_utc"),
         QDateTime(now.date().addDays(1 - exportDays), QTime(0, 0), QTimeZone::UTC)
             .toString(Qt::ISODate)},
        {QStringLiteral("coverage_end_utc"), now.toString(Qt::ISODateWithMs)},
        {QStringLiteral("first_record_utc"), data.summary.fromUtc.toString(Qt::ISODateWithMs)},
        {QStringLiteral("last_record_utc"), data.summary.toUtc.toString(Qt::ISODateWithMs)},
        {QStringLiteral("has_history"), data.summary.hasHistory},
        {QStringLiteral("category_counts"), counts},
        {QStringLiteral("duration_bucket_counts"), bucketCounts},
        {QStringLiteral("unavailable_categories"), unavailable},
        {QStringLiteral("omitted_categories"),
         QJsonArray{QStringLiteral("sql_text"), QStringLiteral("result_rows"),
                    QStringLiteral("native_crash_report"), QStringLiteral("raw_stack_dump"),
                    QStringLiteral("heap_snapshot")}},
        {QStringLiteral("dropped_records"), double(data.summary.droppedRecords)},
        {QStringLiteral("dropped_records_scope"),
         QStringLiteral("since_last_clear_or_stats_reset")},
        {QStringLiteral("invalid_records_omitted"), double(data.invalidRecords)},
        {QStringLiteral("export_cap_omitted"), double(data.omittedByCap)},
        {QStringLiteral("retention_days"), retentionDays},
        {QStringLiteral("export_days"), exportDays},
        {QStringLiteral("day_byte_cap"), double(maxDayBytes)},
        {QStringLiteral("archive_byte_cap"), double(maxExportBytes)},
        {QStringLiteral("memory_sample_interval_seconds"), 60},
        {QStringLiteral("event_memory_min_interval_seconds"), 5},
        {QStringLiteral("memory_interpretation"),
         QStringLiteral("A memory trend cannot prove an allocation leak.")},
        {QStringLiteral("unclean_exit_interpretation"),
         QStringLiteral("An unclean exit does not identify its cause.")}};
    const auto archive =
        zipEntries({{"manifest.json", jsonLine(manifest)}, {"events.jsonl", data.events}});
    if (archive.size() > maxExportBytes)
        return failed(QStringLiteral("Diagnostics report exceeds its size limit."));
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly))
        return failed(QStringLiteral("Could not open the selected ZIP destination."));
    for (qsizetype offset = 0; offset < archive.size(); offset += 64 * 1024) {
        if (cancel && cancel->load()) {
            output.cancelWriting();
            return cancelled();
        }
        const auto size = std::min<qsizetype>(64 * 1024, archive.size() - offset);
        if (output.write(archive.constData() + offset, size) != size) {
            output.cancelWriting();
            return failed(QStringLiteral("Could not write the diagnostics ZIP."));
        }
    }
    if (cancel && cancel->load()) {
        output.cancelWriting();
        return cancelled();
    }
    if (!output.commit())
        return failed(QStringLiteral("Could not save the diagnostics ZIP."));
    record({.event = DiagnosticEvent::ExportSucceeded});
    return {.success = true};
}
bool DiagnosticsService::clear(QString* error) {
    flush();
    std::lock_guard io(state_->ioMutex);
    DiagnosticsFileLock fileLock(state_->folder);
    if (!fileLock.lock(3000)) {
        if (error)
            *error = QStringLiteral("Could not lock local diagnostics for clearing.");
        return false;
    }
    if (QFileInfo(state_->folder).isSymLink()) {
        if (error)
            *error = QStringLiteral("Could not clear all local diagnostics.");
        return false;
    }
    removeStaleMarkers(state_->folder);
    QDir directory(state_->folder);
    for (const auto& name :
         directory.entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot)) {
        if (isDayFile(name) && !QFile::remove(directory.filePath(name))) {
            if (error)
                *error = QStringLiteral("Could not clear all local diagnostics.");
            return false;
        }
    }
    const auto stats = directory.filePath(QStringLiteral("stats.json"));
    if (QFileInfo::exists(stats) && !QFile::remove(stats)) {
        if (error)
            *error = QStringLiteral("Could not clear all local diagnostics.");
        return false;
    }
    state_->dropped = 0;
    state_->syncedDropped = 0;
    state_->lastMemory = {};
    if (error)
        error->clear();
    return true;
}
QString DiagnosticsService::folderPath() const {
    return state_->folder;
}
QString DiagnosticsService::warning() const {
    std::lock_guard lock(state_->queueMutex);
    return state_->lastWarning;
}
} // namespace choscordb
