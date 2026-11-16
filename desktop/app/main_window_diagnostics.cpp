#include "app/main_window.h"

#include "app/diagnostics_service.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "bridge/rust_text.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/field/field.h"
#include "design_system/metrics/metrics.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include <QDesktopServices>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
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

using bridge_detail::fromRust;

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
        const auto source =
            diagnostics_export_zip(*backend_, bridge_detail::utf8View(path), cancellation);
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
        "The diagnostics bundle covers the last 7 days of local diagnostics: typed errors, "
        "lifecycle and activity counts, timing buckets, UI stalls, and memory trends. "
        "Memory growth does not prove a leak. Review the diagnostics bundle before you attach it "
        "to a support report.");
    const bool snapshotUnavailable =
        summary.unavailableCategories.contains(QStringLiteral("diagnostic_snapshot"));
    if (snapshotUnavailable)
        text += QObject::tr("\nThe size and contents are temporarily unavailable while "
                            "diagnostic storage is busy.");
    else
        text += QObject::tr("\nEstimated bundle size: %1.")
                    .arg(QLocale().formattedDataSize(qint64(summary.estimatedBytes)));
    if (!summary.hasHistory && !snapshotUnavailable)
        text += QObject::tr("\nNo diagnostic history is available yet. The diagnostics bundle "
                            "contains only a manifest that tells this.");
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
}

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
    tabCount->setInterval(EngineAdapter::tabCountSampleMs());
    connect(tabCount, &QTimer::timeout, this, [this] {
        if (diagnostics_)
            diagnostics_->setOpenTabs(editors_ ? editors_->count() : 0);
    });
    tabCount->start();
    auto* sampler = new QTimer(this);
    sampler->setInterval(std::chrono::seconds(diagnostics_memory_sample_interval_seconds()));
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
            summary->setText(tr("Local diagnostics are not available."));
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
    dialog->setWindowTitle(tr("Export diagnostics"));
    dialog->setAppModal();
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto* sections = dialog->createSections();
    auto* heading = new design::Text(dialog->windowTitle(), sections);
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss = sections->addDismissButton(tr("Close diagnostics export"));
    dismiss->setObjectName("diagnosticsDismiss");
    connect(dismiss, &QPushButton::clicked, dialog, &QDialog::reject);
    auto* summary =
        dialog->createDescription(diagnostics_ ? tr("Reading local diagnostics…")
                                               : tr("Local diagnostics are not available."),
                                  sections);
    summary->setObjectName("diagnosticsSummary");
    sections->bodyLayout()->addWidget(summary);
    auto* destinationRow = new QWidget(sections);
    auto* destinationLayout = new QHBoxLayout(destinationRow);
    destinationLayout->setContentsMargins(0, 0, 0, 0);
    destinationLayout->setSpacing(design::spacing(design::Spacing::Two));
    auto* destination = new QLineEdit(destinationRow);
    destination->setObjectName("diagnosticsDestination");
    destination->setAccessibleName(tr("Diagnostics bundle destination"));
    destination->setEnabled(diagnostics_ != nullptr);
    auto* destinationValidation = new design::FieldValidation(destination, destinationRow);
    destinationLayout->addWidget(destinationValidation, 1);
    auto* browse = new design::Button(tr("Browse…"), destinationRow);
    browse->setObjectName("diagnosticsBrowse");
    browse->setAccessibleName(tr("Choose diagnostics bundle destination"));
    browse->setVariant(design::ButtonVariant::Outline);
    browse->setEnabled(diagnostics_ != nullptr);
    destinationLayout->addWidget(browse);
    auto* form = sections->addForm();
    sections->addFormRow(form, tr("Destination"), destinationRow);
    auto* clear = sections->addAction(design::DialogActionRole::DestructiveUtility,
                                      tr("Clear local diagnostics…"));
    clear->setObjectName("diagnosticsClear");
    clear->setEnabled(false);
    auto* folder =
        sections->addAction(design::DialogActionRole::Utility, tr("Show diagnostics folder"));
    folder->setObjectName("diagnosticsShowFolder");
    folder->setEnabled(diagnostics_ != nullptr);
    auto* close = sections->addAction(design::DialogActionRole::Dismiss, tr("Cancel"));
    close->setObjectName("diagnosticsCancel");
    auto* save =
        sections->addAction(design::DialogActionRole::Primary, tr("Save diagnostics bundle"));
    save->setObjectName("diagnosticsSave");
    save->setEnabled(false);
    auto* status = sections->addStatusLine();
    status->setObjectName("diagnosticsStatus");
    const auto previewReady = std::make_shared<bool>(false);
    connect(destination, &QLineEdit::textChanged, dialog,
            [destinationValidation] { destinationValidation->setError({}); });
    connect(browse, &QPushButton::clicked, dialog, [destination, dialog] {
        const auto path =
            QFileDialog::getSaveFileName(dialog, tr("Save diagnostics bundle"), destination->text(),
                                         tr("Diagnostics bundles (*.zip)"));
        if (!path.isEmpty())
            destination->setText(path);
    });
    connect(close, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(folder, &QPushButton::clicked, dialog, [this, status] {
        if (!diagnostics_ ||
            !QDesktopServices::openUrl(QUrl::fromLocalFile(diagnostics_->folderPath())))
            status->showError(tr("Could not open the diagnostics folder."));
    });
    connect(
        clear, &QPushButton::clicked, dialog,
        [this, dialog, summary, status, clear, save, previewReady] {
            auto* confirmation = new ConfirmationDialog(
                QMessageBox::Warning, tr("Clear local diagnostics"),
                tr("Delete only local diagnostic records? Saved connections, SQL history, recovery "
                   "data, credentials, and preferences remain available. Capture resumes "
                   "immediately."),
                QMessageBox::Cancel, dialog);
            confirmation->setObjectName("diagnosticsClearConfirmation");
            auto* accept = confirmation->addActionButton(
                tr("Clear diagnostics"), ConfirmationDialog::Consequence::Destructive);
            confirmation->setAttribute(Qt::WA_DeleteOnClose);
            connect(
                confirmation, &QDialog::finished, dialog,
                [this, dialog, confirmation, accept, summary, status, clear, save, previewReady] {
                    if (confirmation->clickedButton() != accept || !diagnostics_)
                        return;
                    clear->setEnabled(false);
                    save->setEnabled(false);
                    *previewReady = false;
                    status->showProgress(tr("Clearing local diagnostics…"));
                    auto* task = new BackgroundTask<ClearOutcome>(this);
                    const QPointer<DialogShell> guard(dialog);
                    auto service = std::make_shared<DiagnosticsWorker>(diagnostics_->backend());
                    task->start(
                        [service = std::move(service)] { return service->clearThenPreview(); },
                        [this, guard, summary, status, clear, save,
                         previewReady](const ClearOutcome& outcome) {
                            if (!guard || !diagnostics_)
                                return;
                            *previewReady = true;
                            clear->setEnabled(true);
                            save->setEnabled(true);
                            if (!outcome.success) {
                                status->showError(
                                    tr("Could not clear diagnostics: %1").arg(outcome.error));
                                return;
                            }
                            summary->setText(summaryText(outcome.summary));
                            status->showSuccess(
                                tr("Local diagnostics cleared. Capture has resumed."));
                        });
                });
            confirmation->open();
        });
    connect(
        save, &QPushButton::clicked, dialog,
        [this, dialog, destination, destinationValidation, browse, folder, clear, save, status] {
            if (!diagnostics_)
                return;
            if (destination->text().trimmed().isEmpty()) {
                destinationValidation->setError(tr("Choose a destination."));
                destination->setFocus();
                return;
            }
            const auto chosen = destination->text().toUtf8();
            const QString path =
                fromRust(diagnostics_export_destination(bridge_detail::utf8View(chosen)));
            save->setEnabled(false);
            auto* preflight = new BackgroundTask<DocumentPathStatusDto>(this);
            const QPointer<DialogShell> guard(dialog);
            preflight->start(
                [path] {
                    const auto bytes = path.toUtf8();
                    return document_path_status(bridge_detail::utf8View(bytes));
                },
                [this, guard, dialog, destination, browse, folder, clear, save, status,
                 path](const DocumentPathStatusDto& result) {
                    if (!guard || !guard->isVisible() || !diagnostics_)
                        return;
                    save->setEnabled(true);
                    if (!result.error.empty()) {
                        status->showError(
                            tr("Could not check the destination: %1").arg(fromRust(result.error)));
                        return;
                    }
                    if (result.exists &&
                        !ConfirmationDialog::confirm(
                            this, tr("Replace diagnostics bundle"),
                            tr("Replace the existing file at the chosen destination?"),
                            tr("Replace"), ConfirmationDialog::Consequence::Destructive))
                        return;
                    if (!guard || !guard->isVisible() || !diagnostics_)
                        return;
                    save->setEnabled(false);
                    destination->setEnabled(false);
                    browse->setEnabled(false);
                    folder->setEnabled(false);
                    clear->setEnabled(false);
                    status->showProgress(tr("Creating diagnostics bundle…"));
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
                                dialogGuard->closeWithSuccess(
                                    tr("Diagnostics bundle saved"),
                                    tr("Saved to %1. Inspect the bundle, then attach it to your "
                                       "support conversation.")
                                        .arg(path));
                            else if (result.cancelled)
                                status->showInfo(tr("Diagnostics export cancelled."));
                            else
                                status->showError(
                                    tr("Could not export diagnostics: %1").arg(result.error));
                        });
                });
        });
    dialog->resize(design::dialogInitialSize(design::DialogSize::Preferences));
    dialog->open();
    if (diagnostics_) {
        auto* task = new BackgroundTask<DiagnosticSummary>(this);
        const QPointer<DialogShell> guard(dialog);
        auto service = std::make_shared<DiagnosticsWorker>(diagnostics_->backend());
        task->start(
            [service = std::move(service)] { return service->preview(); },
            [this, guard, summary, clear, save, previewReady](const DiagnosticSummary& result) {
                if (!guard || !diagnostics_)
                    return;
                auto text = summaryText(result);
                if (!diagnostics_->warning().isEmpty())
                    text += tr("\nWarning: %1").arg(diagnostics_->warning());
                summary->setText(text);
                *previewReady = true;
                clear->setEnabled(true);
                save->setEnabled(true);
            });
    }
}

}
