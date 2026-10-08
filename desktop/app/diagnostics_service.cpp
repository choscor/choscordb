#include "app/diagnostics_service.h"
#include "bridge/rust_text.h"
#include <algorithm>
#include <chrono>
#include <thread>

namespace choscordb {
namespace {
using bridge_detail::fromRust;
using bridge_detail::utf8View;
} // namespace

struct DiagnosticsService::State {
    rust::Box<RustDiagnostics> service;
    explicit State(const QString& directory, const QString& version, const QString& build)
        : service([&] {
              const auto pathBytes = directory.toUtf8();
              const auto versionBytes = version.toUtf8();
              const auto buildBytes = build.toUtf8();
              return diagnostics_new(utf8View(pathBytes), utf8View(versionBytes),
                                     utf8View(buildBytes));
          }()) {}
};

DiagnosticsService::DiagnosticsService(QString applicationDataDirectory, QString appVersion,
                                       QString buildVersion)
    : state_(std::make_unique<State>(applicationDataDirectory, appVersion, buildVersion)) {}
DiagnosticsService::~DiagnosticsService() {
    stop();
}
bool DiagnosticsService::start() {
    return diagnostics_start(*state_->service);
}
void DiagnosticsService::stop() {
    diagnostics_stop(*state_->service);
}
void DiagnosticsService::record(DiagnosticRecord record) noexcept {
    DiagnosticRecordDto dto;
    dto.event = static_cast<std::uint16_t>(record.event);
    dto.driver = static_cast<std::uint16_t>(record.driver);
    dto.error_class = static_cast<std::uint16_t>(record.errorClass);
    dto.duration_bucket = static_cast<std::uint16_t>(record.durationBucket);
    dto.open_tabs = record.openTabs;
    dto.duration_ms = record.durationMs;
    dto.force_memory = record.forceMemory;
    diagnostics_record(*state_->service, dto);
}
void DiagnosticsService::sampleMemory(int openTabs, bool force) noexcept {
    diagnostics_sample_memory(*state_->service, openTabs, force);
}
void DiagnosticsService::setOpenTabs(int openTabs) noexcept {
    diagnostics_set_open_tabs(*state_->service, openTabs);
}
void DiagnosticsService::flush() {
    diagnostics_flush(*state_->service);
}
DiagnosticSummary DiagnosticsService::preview() {
    const auto source = diagnostics_preview(*state_->service);
    DiagnosticSummary result;
    result.estimatedBytes = static_cast<qint64>(source.estimated_bytes);
    for (const auto& count : source.category_counts)
        result.categoryCounts.insert(fromRust(count.name), static_cast<int>(count.count));
    for (const auto& count : source.duration_bucket_counts)
        result.durationBucketCounts.insert(fromRust(count.name), static_cast<int>(count.count));
    result.fromUtc = QDateTime::fromString(fromRust(source.from_utc), Qt::ISODateWithMs);
    result.toUtc = QDateTime::fromString(fromRust(source.to_utc), Qt::ISODateWithMs);
    for (const auto& name : source.unavailable_categories)
        result.unavailableCategories.append(fromRust(name));
    result.droppedRecords = source.dropped_records;
    result.hasHistory = source.has_history;
    return result;
}
DiagnosticExportResult DiagnosticsService::exportZip(const QString& destination,
                                                     const std::atomic_bool* cancel) {
    auto token = diagnostics_new_cancellation();
    if (cancel && cancel->load())
        diagnostics_cancel(*token);
    std::atomic_bool finished = false;
    std::thread watcher;
    if (cancel && !cancel->load()) {
        try {
            watcher = std::thread([cancel, &token, &finished] {
                while (!finished.load()) {
                    if (cancel->load()) {
                        diagnostics_cancel(*token);
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            });
        } catch (...) {
            return {.error = QStringLiteral("Diagnostics export unavailable.")};
        }
    }
    const auto bytes = destination.toUtf8();
    const auto source = diagnostics_export_zip(*state_->service, utf8View(bytes), *token);
    finished = true;
    if (watcher.joinable())
        watcher.join();
    return {
        .success = source.success, .cancelled = source.cancelled, .error = fromRust(source.error)};
}
bool DiagnosticsService::clear(QString* error) {
    const auto source = diagnostics_clear(*state_->service);
    if (error)
        *error = fromRust(source.error);
    return source.success;
}
QString DiagnosticsService::folderPath() const {
    return fromRust(diagnostics_folder_path(*state_->service));
}
QString DiagnosticsService::warning() const {
    return fromRust(diagnostics_warning(*state_->service));
}
const RustDiagnostics& DiagnosticsService::backend() const {
    return *state_->service;
}
void DiagnosticsService::observeCommandFailure() {
    diagnostics_observe_command_failure(*state_->service);
}
} // namespace choscordb
