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
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
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

QString fromRust(const rust::String& value) {
    return QString::fromUtf8(value.data(), qsizetype(value.size()));
}

class DiagnosticsWorker final {
  public:
    explicit DiagnosticsWorker(const RustDiagnostics& source)
        : backend_(diagnostics_clone(source)) {}

    DiagnosticSummary preview() const {
        const auto source = diagnostics_preview(*backend_);
        DiagnosticSummary result;
        result.estimatedBytes = qint64(source.estimated_bytes);
        for (const auto& count : source.category_counts)
            result.categoryCounts.insert(fromRust(count.name), int(count.count));
        for (const auto& count : source.duration_bucket_counts)
            result.durationBucketCounts.insert(fromRust(count.name), int(count.count));
        result.fromUtc = QDateTime::fromString(fromRust(source.from_utc), Qt::ISODateWithMs);
        result.toUtc = QDateTime::fromString(fromRust(source.to_utc), Qt::ISODateWithMs);
        for (const auto& name : source.unavailable_categories)
            result.unavailableCategories.append(fromRust(name));
        result.droppedRecords = source.dropped_records;
        result.hasHistory = source.has_history;
        return result;
    }

    ClearOutcome clearThenPreview() const {
        const auto source = diagnostics_clear(*backend_);
        ClearOutcome result;
        result.success = source.success;
        result.error = fromRust(source.error);
        if (result.success)
            result.summary = preview();
        return result;
    }

    DiagnosticExportResult exportZip(const QString& destination,
                                     const DiagnosticCancellation& cancellation) const {
        const auto path = destination.toUtf8();
        const auto source = diagnostics_export_zip(
            *backend_, rust::Str(path.constData(), size_t(path.size())), cancellation);
        return {.success = source.success,
                .cancelled = source.cancelled,
                .error = fromRust(source.error)};
    }

  private:
    rust::Box<RustDiagnostics> backend_;
};

class ExportTask final : public QObject {
  public:
    explicit ExportTask(QObject* parent)
        : QObject(parent), cancellation_(std::make_shared<rust::Box<DiagnosticCancellation>>(
                               diagnostics_new_cancellation())) {}
    ~ExportTask() override { cancel(); }
    void cancel() { diagnostics_cancel(**cancellation_); }
    void start(std::shared_ptr<DiagnosticsWorker> service, QString destination,
               std::function<void(const DiagnosticExportResult&)> completed) {
        connect(&watcher_, &QFutureWatcher<DiagnosticExportResult>::finished, this,
                [this, completed = std::move(completed)] {
                    completed(watcher_.result());
                    deleteLater();
                });
        const auto cancellation = cancellation_;
        watcher_.setFuture(QtConcurrent::run(
            [service = std::move(service), cancellation, destination = std::move(destination)] {
                return service->exportZip(destination, **cancellation);
            }));
    }

  private:
    std::shared_ptr<rust::Box<DiagnosticCancellation>> cancellation_;
    QFutureWatcher<DiagnosticExportResult> watcher_;
};

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
    auto* adapter = workspace_->adapter();
    adapter->attachDiagnostics(diagnostics_->backend());
    connect(workspace_, &QueryWorkspace::connectionReady, this, [this](quint64) {
        if (diagnostics_)
            diagnostics_->sampleMemory(editors_ ? editors_->count() : 0, true);
    });
    connect(adapter, &EngineAdapter::commandFailed, this, [this](const QString&) {
        if (diagnostics_)
            diagnostics_->observeCommandFailure();
    });
    diagnostics_->sampleMemory(editors_ ? editors_->count() : 0);
    if (editors_)
        connect(editors_, &QTabWidget::currentChanged, this, [this](int) {
            if (diagnostics_)
                diagnostics_->setOpenTabs(editors_->count());
        });
    auto* tabCount = new QTimer(this);
    tabCount->setInterval(1000);
    connect(tabCount, &QTimer::timeout, this, [this] {
        if (diagnostics_)
            diagnostics_->setOpenTabs(editors_ ? editors_->count() : 0);
    });
    tabCount->start();
    auto* sampler = new QTimer(this);
    sampler->setInterval(60 * 1000);
    connect(sampler, &QTimer::timeout, this, [this] {
        if (diagnostics_)
            diagnostics_->sampleMemory(editors_ ? editors_->count() : 0);
    });
    sampler->start();
}

void MainWindow::disableDiagnostics() {
    diagnostics_ = nullptr;
    if (auto* dialog = findChild<DialogShell*>("diagnosticsExportDialog")) {
        if (auto* summary = dialog->findChild<QLabel*>("diagnosticsSummary"))
            summary->setText(tr("Local diagnostics are unavailable in this session."));
        for (const auto* name : {"diagnosticsDestination", "diagnosticsBrowse",
                                 "diagnosticsShowFolder", "diagnosticsClear", "diagnosticsSave"}) {
            if (auto* control = dialog->findChild<QWidget*>(name))
                control->setEnabled(false);
        }
    }
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
    destination->setEnabled(diagnostics_ != nullptr);
    destinationLabel->setBuddy(destination);
    destinationRow->addWidget(destinationLabel);
    destinationRow->addWidget(destination, 1);
    auto* browse = new design::Button(tr("Browse…"), sections);
    browse->setObjectName("diagnosticsBrowse");
    browse->setAccessibleName(tr("Choose diagnostic ZIP destination"));
    browse->setVariant(design::ButtonVariant::Outline);
    browse->setEnabled(diagnostics_ != nullptr);
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
    close->setVariant(design::ButtonVariant::Outline);
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
                        auto service = std::make_shared<DiagnosticsWorker>(diagnostics_->backend());
                        task->start(
                            [service = std::move(service)] { return service->clearThenPreview(); },
                            [this, guard, summary, status, clear, save, destination,
                             previewReady](const ClearOutcome& outcome) {
                                if (!guard || !diagnostics_)
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
    connect(
        save, &QPushButton::clicked, dialog,
        [this, dialog, destination, browse, folder, clear, save, status] {
            if (!diagnostics_)
                return;
            QString path = destination->text().trimmed();
            if (!path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
                path += QStringLiteral(".zip");
            save->setEnabled(false);
            auto* preflight = new BackgroundTask<DocumentPathStatusDto>(this);
            const QPointer<DialogShell> guard(dialog);
            preflight->start(
                [path] {
                    const auto bytes = path.toUtf8();
                    return document_path_status(rust::Str(bytes.constData(), size_t(bytes.size())));
                },
                [this, guard, dialog, destination, browse, folder, clear, save, status,
                 path](const DocumentPathStatusDto& result) {
                    if (!guard || !guard->isVisible() || !diagnostics_)
                        return;
                    save->setEnabled(true);
                    if (!result.error.empty()) {
                        status->setText(
                            tr("Could not check destination: %1").arg(fromRust(result.error)));
                        return;
                    }
                    if (result.exists) {
                        const auto answer = ConfirmationDialog::question(
                            this, tr("Replace Diagnostic ZIP"),
                            tr("Replace the existing ZIP at the chosen destination?"),
                            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
                        if (answer != QMessageBox::Yes)
                            return;
                    }
                    // The nested confirmation event loop may process a failed async
                    // diagnostics startup and disable this service before returning.
                    if (!guard || !guard->isVisible() || !diagnostics_)
                        return;
                    save->setEnabled(false);
                    destination->setEnabled(false);
                    browse->setEnabled(false);
                    folder->setEnabled(false);
                    clear->setEnabled(false);
                    status->setText(tr("Creating diagnostic ZIP…"));
                    auto* task = new ExportTask(this);
                    connect(dialog, &QDialog::finished, task, [task] { task->cancel(); });
                    const QPointer<DialogShell> dialogGuard(dialog);
                    auto service = std::make_shared<DiagnosticsWorker>(diagnostics_->backend());
                    task->start(
                        std::move(service), path,
                        [this, dialogGuard, destination, browse, folder, clear, save, status,
                         path](const DiagnosticExportResult& result) {
                            if (!dialogGuard || !diagnostics_)
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
        });
    dialog->resize(design::dialogInitialSize(design::DialogSize::Export));
    dialog->open();
    if (diagnostics_) {
        auto* task = new BackgroundTask<DiagnosticSummary>(this);
        const QPointer<DialogShell> guard(dialog);
        auto service = std::make_shared<DiagnosticsWorker>(diagnostics_->backend());
        task->start([service = std::move(service)] { return service->preview(); },
                    [this, guard, summary, clear, save, destination,
                     previewReady](const DiagnosticSummary& result) {
                        if (!guard || !diagnostics_)
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
