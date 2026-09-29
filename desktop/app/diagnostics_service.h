#pragma once

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>
#include <atomic>
#include <cstdint>
#include <memory>
class QObject;

namespace choscordb {

// Schema 1. Keep this contract free of SQL, names, paths, and driver error text.
enum class DiagnosticEvent {
    Startup,
    Shutdown,
    ConnectionSucceeded,
    ConnectionFailed,
    QuerySucceeded,
    QueryFailed,
    ResultPage,
    ExportSucceeded,
    ExportFailed,
    Cancelled,
    UiHangStart,
    UiHangEnd,
    UncleanExit,
    Error,
    MemorySample
};
enum class DiagnosticDriver { Unknown, SQLite, PostgreSQL, MySQL };
enum class DiagnosticErrorClass { Unknown, Connection, Authentication, Query, IO, Internal };
enum class DiagnosticDurationBucket { Unknown, Under100Ms, Under1s, Under10s, Over10s };

struct DiagnosticRecord {
    DiagnosticEvent event = DiagnosticEvent::Error;
    DiagnosticDriver driver = DiagnosticDriver::Unknown;
    DiagnosticErrorClass errorClass = DiagnosticErrorClass::Unknown;
    DiagnosticDurationBucket durationBucket = DiagnosticDurationBucket::Unknown;
    int openTabs = 0;
    int durationMs = 0;       // Completed UI hangs only; capped at ten minutes.
    bool forceMemory = false; // An event boundary may request the five-second cadence.
};

struct DiagnosticSummary {
    qint64 estimatedBytes = 0;
    QMap<QString, int> categoryCounts;
    QMap<QString, int> durationBucketCounts;
    QDateTime fromUtc;
    QDateTime toUtc;
    QStringList unavailableCategories;
    quint64 droppedRecords = 0;
    bool hasHistory = false;
};

struct DiagnosticExportResult {
    bool success = false;
    bool cancelled = false;
    QString error;
};

class DiagnosticsService {
  public:
    DiagnosticsService(QString applicationDataDirectory, QString appVersion,
                       QString buildVersion = {});
    ~DiagnosticsService();
    DiagnosticsService(const DiagnosticsService&) = delete;
    DiagnosticsService& operator=(const DiagnosticsService&) = delete;

    bool start();
    void stop();
    void record(DiagnosticRecord record) noexcept;
    void sampleMemory(int openTabs = 0, bool force = false) noexcept;
    void flush();
    DiagnosticSummary preview();
    DiagnosticExportResult exportZip(const QString& destination,
                                     const std::atomic_bool* cancel = nullptr);
    bool clear(QString* error = nullptr);
    QString folderPath() const;
    QString warning() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

// The caller must stop this before mainContext or service is destroyed.
class DiagnosticsWatchdog {
  public:
    DiagnosticsWatchdog(QObject* mainContext, DiagnosticsService* service);
    ~DiagnosticsWatchdog();
    DiagnosticsWatchdog(const DiagnosticsWatchdog&) = delete;
    DiagnosticsWatchdog& operator=(const DiagnosticsWatchdog&) = delete;
    void start();
    void stop();

  private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace choscordb
