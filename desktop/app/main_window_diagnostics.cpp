#include "app/main_window.h"

#include "app/diagnostics_service.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/metrics/metrics.h"
#include "design_system/text/text.h"
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <functional>
#include <memory>

namespace choscordb {
namespace {
template <typename Result> class BackgroundTask final : public QObject {
  public:
    explicit BackgroundTask(QObject* parent) : QObject(parent) {}
    ~BackgroundTask() override { watcher_.waitForFinished(); }
    void start(std::function<Result()> work, std::function<void(const Result&)> completed) {
        connect(&watcher_, &QFutureWatcher<Result>::finished, this,
                [this, completed = std::move(completed)] {
                    completed(watcher_.result());
                    deleteLater();
                });
        watcher_.setFuture(QtConcurrent::run(std::move(work)));
    }

  private:
    QFutureWatcher<Result> watcher_;
};

struct ClearOutcome {
    bool success = false;
    QString error;
    DiagnosticSummary summary;
};

class ExportTask final : public QObject {
  public:
    explicit ExportTask(QObject* parent) : QObject(parent) {}
    ~ExportTask() override {
        cancel();
        watcher_.waitForFinished();
    }
    void cancel() { cancelled_ = true; }
    void start(DiagnosticsService* service, QString destination,
               std::function<void(const DiagnosticExportResult&)> completed) {
        connect(&watcher_, &QFutureWatcher<DiagnosticExportResult>::finished, this,
                [this, completed = std::move(completed)] {
                    completed(watcher_.result());
                    deleteLater();
                });
        watcher_.setFuture(QtConcurrent::run([this, service, destination = std::move(destination)] {
            return service->exportZip(destination, &cancelled_);
        }));
    }

  private:
    std::atomic_bool cancelled_ = false;
    QFutureWatcher<DiagnosticExportResult> watcher_;
};

DiagnosticDriver safeDriver(const QString& driver) {
    if (driver == QLatin1String("sqlite"))
        return DiagnosticDriver::SQLite;
    if (driver == QLatin1String("postgres"))
        return DiagnosticDriver::PostgreSQL;
    if (driver == QLatin1String("mysql"))
        return DiagnosticDriver::MySQL;
    return DiagnosticDriver::Unknown;
}
DiagnosticDurationBucket durationBucket(quint64 durationMs) {
    if (durationMs < 100)
        return DiagnosticDurationBucket::Under100Ms;
    if (durationMs < 1000)
        return DiagnosticDurationBucket::Under1s;
    if (durationMs < 10000)
        return DiagnosticDurationBucket::Under10s;
    return DiagnosticDurationBucket::Over10s;
}
QString summaryText(const DiagnosticSummary& summary) {
    QString text = QObject::tr(
        "The standard ZIP covers the last 7 days of local diagnostics: typed errors, "
        "lifecycle and activity counts, timing buckets, UI stalls, and memory trends. "
        "Memory growth does not prove a leak. Review the ZIP before attaching it manually "
        "to a support conversation.");
    const bool snapshotUnavailable =
        summary.unavailableCategories.contains(QStringLiteral("diagnostic_snapshot"));
    if (snapshotUnavailable)
        text += QObject::tr("\nThe size and contents are temporarily unavailable while "
                            "diagnostic storage is busy.");
    else
        text += QObject::tr("\nEstimated bundle size: %1 KiB.")
                    .arg((summary.estimatedBytes + 1023) / 1024);
    if (!summary.hasHistory && !snapshotUnavailable)
        text += QObject::tr("\nNo diagnostic history is available yet; the ZIP will contain "
                            "a manifest describing this.");
    else if (!snapshotUnavailable) {
        text += QObject::tr("\nCoverage: %1 to %2 UTC.")
                    .arg(summary.fromUtc.toUTC().toString(Qt::ISODate),
                         summary.toUtc.toUTC().toString(Qt::ISODate));
        QStringList categories;
        for (auto it = summary.categoryCounts.cbegin(); it != summary.categoryCounts.cend(); ++it)
            categories << QObject::tr("%1: %2").arg(it.key()).arg(it.value());
        text += QObject::tr("\nCategories: %1.")
                    .arg(categories.isEmpty() ? QObject::tr("none") : categories.join(", "));
    }
    if (!summary.unavailableCategories.isEmpty())
        text += QObject::tr("\nUnavailable: %1.").arg(summary.unavailableCategories.join(", "));
    if (summary.droppedRecords)
        text += QObject::tr("\nDropped records: %1.").arg(summary.droppedRecords);
    return text;
}
} // namespace

void MainWindow::connectDiagnostics() {
    if (!diagnostics_ || !workspace_)
        return;
    connect(workspace_, &QueryWorkspace::connectionReady, this, [this](quint64 connection) {
        diagnostics_->record({.event = DiagnosticEvent::ConnectionSucceeded,
                              .driver = safeDriver(workspace_->driverForConnection(connection)),
                              .openTabs = editors_ ? editors_->count() : 0});
        diagnostics_->sampleMemory(editors_ ? editors_->count() : 0, true);
    });
    connect(workspace_, &QueryWorkspace::connectionAttemptFailed, this,
            [this](const QString& driver) {
                diagnostics_->record({.event = DiagnosticEvent::ConnectionFailed,
                                      .driver = safeDriver(driver),
                                      .errorClass = DiagnosticErrorClass::Connection,
                                      .openTabs = editors_ ? editors_->count() : 0});
            });
    auto* adapter = workspace_->adapter();
    auto queryStarts = std::make_shared<QHash<quint64, QElapsedTimer>>();
    connect(
        adapter, &EngineAdapter::eventReady, this, [this, queryStarts](const BridgeEvent& event) {
            const auto kind = QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
            if (kind == QLatin1String("query_state")) {
                const auto state =
                    QString::fromUtf8(event.state.data(), qsizetype(event.state.size()));
                if (state == QLatin1String("queued") && !queryStarts->contains(event.id)) {
                    if (queryStarts->size() >= 1024)
                        queryStarts->clear();
                    QElapsedTimer timer;
                    timer.start();
                    queryStarts->insert(event.id, timer);
                }
                return;
            }
            DiagnosticRecord record;
            record.openTabs = editors_ ? editors_->count() : 0;
            if (kind == QLatin1String("query_finished")) {
                record.event = DiagnosticEvent::QuerySucceeded;
                record.durationBucket = durationBucket(event.duration_ms);
                queryStarts->remove(event.id);
            } else if (kind == QLatin1String("query_failed")) {
                const auto errorKind =
                    QString::fromUtf8(event.error_kind.data(), qsizetype(event.error_kind.size()));
                record.event = errorKind == QLatin1String("Cancelled")
                                   ? DiagnosticEvent::Cancelled
                                   : DiagnosticEvent::QueryFailed;
                record.errorClass = DiagnosticErrorClass::Query;
                const auto start = queryStarts->find(event.id);
                if (start != queryStarts->end()) {
                    record.durationBucket =
                        durationBucket(quint64(qMax<qint64>(0, start->elapsed())));
                    queryStarts->erase(start);
                }
            } else if (kind == QLatin1String("stored_page")) {
                record.event = DiagnosticEvent::ResultPage;
            } else if (kind == QLatin1String("export_finished")) {
                record.event = DiagnosticEvent::ExportSucceeded;
            } else if (kind == QLatin1String("export_failed")) {
                record.event = DiagnosticEvent::ExportFailed;
                record.errorClass = DiagnosticErrorClass::IO;
            } else {
                return;
            }
            diagnostics_->record(record);
            if (record.event == DiagnosticEvent::QuerySucceeded ||
                record.event == DiagnosticEvent::QueryFailed)
                diagnostics_->sampleMemory(record.openTabs, true);
        });
    connect(adapter, &EngineAdapter::commandFailed, this, [this](const QString&) {
        diagnostics_->record(
            {.event = DiagnosticEvent::Error, .errorClass = DiagnosticErrorClass::Internal});
    });
    diagnostics_->sampleMemory(editors_ ? editors_->count() : 0);
    auto* sampler = new QTimer(this);
    sampler->setInterval(60 * 1000);
    connect(sampler, &QTimer::timeout, this,
            [this] { diagnostics_->sampleMemory(editors_ ? editors_->count() : 0); });
    sampler->start();
}

void MainWindow::showDiagnosticsExport() {
    auto* dialog = findChild<DialogShell*>("diagnosticsExportDialog");
    if (dialog) {
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    dialog = new DialogShell(this);
    dialog->setObjectName("diagnosticsExportDialog");
    dialog->setWindowTitle(tr("Export Diagnostics"));
    dialog->setAppModal();
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* sections = new design::DialogSections(dialog);
    layout->addWidget(sections);
    auto* heading = new design::Text(tr("Export Diagnostics"), sections);
    heading->setTypographyRole(design::TypographyRole::DialogTitle);
    sections->headerLayout()->addWidget(heading);
    auto* summary = dialog->createDescription(
        diagnostics_ ? tr("Reading local diagnostics…")
                     : tr("Local diagnostics are unavailable in this session."),
        sections);
    summary->setObjectName("diagnosticsSummary");
    sections->bodyLayout()->addWidget(summary);
    auto* destinationRow = new QHBoxLayout;
    auto* destinationLabel = new QLabel(tr("ZIP destination:"), sections);
    auto* destination = new QLineEdit(sections);
    destination->setObjectName("diagnosticsDestination");
    destination->setAccessibleName(tr("Diagnostic ZIP destination"));
    destinationLabel->setBuddy(destination);
    destinationRow->addWidget(destinationLabel);
    destinationRow->addWidget(destination, 1);
    auto* browse = new design::Button(tr("Browse…"), sections);
    browse->setObjectName("diagnosticsBrowse");
    browse->setAccessibleName(tr("Choose diagnostic ZIP destination"));
    browse->setVariant(design::ButtonVariant::Outline);
    destinationRow->addWidget(browse);
    sections->bodyLayout()->addLayout(destinationRow);
    auto* folder = new design::Button(tr("Show Diagnostics Folder"), sections);
    folder->setObjectName("diagnosticsShowFolder");
    folder->setAccessibleName(tr("Show diagnostics folder"));
    folder->setVariant(design::ButtonVariant::Outline);
    folder->setEnabled(diagnostics_ != nullptr);
    sections->footerLayout()->addWidget(folder);
    auto* clear = new design::Button(tr("Clear Local Diagnostics"), sections);
    clear->setObjectName("diagnosticsClear");
    clear->setAccessibleName(tr("Clear local diagnostics"));
    clear->setVariant(design::ButtonVariant::Outline);
    clear->setEnabled(false);
    sections->footerLayout()->addWidget(clear);
    auto* save = new design::Button(tr("Save ZIP…"), sections);
    save->setObjectName("diagnosticsSave");
    save->setAccessibleName(tr("Save diagnostic ZIP"));
    save->setEnabled(false);
    sections->footerLayout()->addWidget(save);
    auto* close = new design::Button(tr("Cancel"), sections);
    close->setObjectName("diagnosticsCancel");
    close->setAccessibleName(tr("Cancel diagnostics export"));
    close->setVariant(design::ButtonVariant::Secondary);
    sections->footerLayout()->addWidget(close);
    auto* status = dialog->createInlineStatus(sections);
    status->setObjectName("diagnosticsStatus");
    sections->bodyLayout()->addWidget(status);
    const auto previewReady = std::make_shared<bool>(false);
    connect(destination, &QLineEdit::textChanged, dialog,
            [this, save, previewReady](const QString& text) {
                save->setEnabled(diagnostics_ && *previewReady && !text.trimmed().isEmpty());
            });
    connect(browse, &QPushButton::clicked, dialog, [destination, dialog] {
        const auto path = QFileDialog::getSaveFileName(
            dialog, tr("Save Diagnostic ZIP"), destination->text(), tr("ZIP files (*.zip)"));
        if (!path.isEmpty())
            destination->setText(path);
    });
    connect(close, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(folder, &QPushButton::clicked, dialog, [this, status] {
        if (!diagnostics_ ||
            !QDesktopServices::openUrl(QUrl::fromLocalFile(diagnostics_->folderPath())))
            status->setText(tr("The diagnostics folder could not be opened."));
    });
    connect(
        clear, &QPushButton::clicked, dialog,
        [this, dialog, summary, status, clear, save, destination, previewReady] {
            auto* confirmation = new ConfirmationDialog(
                QMessageBox::Question, tr("Clear Local Diagnostics"),
                tr("Delete only local diagnostic records? Saved profiles, SQL history, recovery "
                   "data, credentials, and preferences remain available. Capture resumes "
                   "immediately."),
                QMessageBox::Yes | QMessageBox::Cancel, dialog);
            confirmation->setObjectName("diagnosticsClearConfirmation");
            confirmation->setDefaultButton(QMessageBox::Cancel);
            confirmation->setAttribute(Qt::WA_DeleteOnClose);
            connect(confirmation, &QDialog::finished, dialog,
                    [this, dialog, summary, status, clear, save, destination,
                     previewReady](int result) {
                        if (result != QMessageBox::Yes || !diagnostics_)
                            return;
                        clear->setEnabled(false);
                        save->setEnabled(false);
                        *previewReady = false;
                        status->setText(tr("Clearing local diagnostics…"));
                        auto* task = new BackgroundTask<ClearOutcome>(this);
                        const QPointer<DialogShell> guard(dialog);
                        task->start(
                            [service = diagnostics_] {
                                ClearOutcome outcome;
                                outcome.success = service->clear(&outcome.error);
                                if (outcome.success)
                                    outcome.summary = service->preview();
                                return outcome;
                            },
                            [guard, summary, status, clear, save, destination,
                             previewReady](const ClearOutcome& outcome) {
                                if (!guard)
                                    return;
                                *previewReady = true;
                                clear->setEnabled(true);
                                save->setEnabled(!destination->text().trimmed().isEmpty());
                                if (!outcome.success) {
                                    status->setText(
                                        tr("Could not clear diagnostics: %1").arg(outcome.error));
                                    return;
                                }
                                summary->setText(summaryText(outcome.summary));
                                status->setText(
                                    tr("Local diagnostics cleared. Capture has resumed."));
                            });
                    });
            confirmation->open();
        });
    connect(save, &QPushButton::clicked, dialog,
            [this, dialog, destination, browse, folder, clear, save, status] {
                if (!diagnostics_)
                    return;
                QString path = destination->text().trimmed();
                if (!path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
                    path += QStringLiteral(".zip");
                if (QFileInfo::exists(path)) {
                    const auto answer = ConfirmationDialog::question(
                        this, tr("Replace Diagnostic ZIP"),
                        tr("Replace the existing ZIP at the chosen destination?"),
                        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
                    if (answer != QMessageBox::Yes)
                        return;
                }
                save->setEnabled(false);
                destination->setEnabled(false);
                browse->setEnabled(false);
                folder->setEnabled(false);
                clear->setEnabled(false);
                status->setText(tr("Creating diagnostic ZIP…"));
                auto* task = new ExportTask(this);
                connect(dialog, &QDialog::finished, task, [task] { task->cancel(); });
                const QPointer<DialogShell> dialogGuard(dialog);
                task->start(
                    diagnostics_, path,
                    [dialogGuard, destination, browse, folder, clear, save, status,
                     path](const DiagnosticExportResult& result) {
                        if (!dialogGuard)
                            return;
                        destination->setEnabled(true);
                        browse->setEnabled(true);
                        folder->setEnabled(true);
                        clear->setEnabled(true);
                        save->setEnabled(true);
                        if (result.success)
                            status->setText(QObject::tr("Saved to %1. Inspect the ZIP, then "
                                                        "attach it manually to your support "
                                                        "conversation.")
                                                .arg(path));
                        else if (result.cancelled)
                            status->setText(QObject::tr("Diagnostic export cancelled."));
                        else
                            status->setText(
                                QObject::tr("Diagnostic export failed: %1").arg(result.error));
                    });
            });
    dialog->resize(design::dialogInitialSize(design::DialogSize::Export));
    dialog->open();
    if (diagnostics_) {
        auto* task = new BackgroundTask<DiagnosticSummary>(this);
        const QPointer<DialogShell> guard(dialog);
        task->start([service = diagnostics_] { return service->preview(); },
                    [this, guard, summary, clear, save, destination,
                     previewReady](const DiagnosticSummary& result) {
                        if (!guard)
                            return;
                        auto text = summaryText(result);
                        if (!diagnostics_->warning().isEmpty())
                            text += tr("\nWarning: %1").arg(diagnostics_->warning());
                        summary->setText(text);
                        *previewReady = true;
                        clear->setEnabled(true);
                        save->setEnabled(!destination->text().trimmed().isEmpty());
                    });
    }
}

} // namespace choscordb
