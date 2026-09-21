#include "widgets/export_dialog/export_dialog.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/field/field.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace choscordb {
namespace {
QString text(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
} // namespace
ExportDialog::ExportDialog(EngineAdapter* adapter, QWidget* parent)
    : DialogShell(parent), adapter_(adapter), format_(new QComboBox(this)),
      dialect_(new QComboBox(this)), destination_(new QLineEdit(this)),
      schema_(new QLineEdit(this)), table_(new QLineEdit(this)), sqlFields_(new QWidget(this)),
      browse_(new design::Button(tr("&Browse…"), this)),
      start_(new design::Button(tr("&Export"), this)),
      cancel_(new design::Button(tr("Cancel export"), this)), status_(createInlineStatus(this)),
      scope_(createDescription({}, this)) {
    browse_->setVariant(design::ButtonVariant::Outline);
    start_->setDesignIcon(design::Icon::Export);
    cancel_->setVariant(design::ButtonVariant::Secondary);
    setObjectName("exportDialog");
    setWindowTitle(tr("Export results"));
    setAppModal();
    resize(design::dialogInitialSize(design::DialogSize::Export));
    format_->setObjectName("exportFormat");
    destination_->setObjectName("exportDestination");
    schema_->setObjectName("exportSchema");
    table_->setObjectName("exportTable");
    dialect_->setObjectName("exportDialect");
    start_->setObjectName("exportStart");
    cancel_->setObjectName("exportCancel");
    status_->setObjectName("exportStatus");
    format_->addItem(tr("CSV"), "csv");
    format_->addItem(tr("JSON"), "json");
    format_->addItem(tr("JSON Lines"), "jsonl");
    format_->addItem(tr("SQL INSERT"), "sql");
    dialect_->addItem(tr("SQLite"), "sqlite");
    dialect_->addItem(tr("PostgreSQL"), "postgres");
    dialect_->addItem(tr("MySQL"), "mysql");
    schema_->setPlaceholderText(tr("Optional"));
    table_->setToolTip(tr("One literal identifier; dots are not separators."));
    schema_->setToolTip(table_->toolTip());
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    auto* destinationRow = new QHBoxLayout;
    destinationValidation_ = new design::FieldValidation(destination_, this);
    destinationRow->addWidget(destinationValidation_, 1);
    destinationRow->addWidget(browse_, 0, Qt::AlignTop);
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    formatValidation_ = new design::FieldValidation(format_, this);
    form->addRow(tr("&Format:"), formatValidation_);
    auto* destinationLabel = new QLabel(tr("&Destination:"), this);
    destinationLabel->setBuddy(destination_);
    form->addRow(destinationLabel, destinationRow);
    auto* sqlForm = new QFormLayout(sqlFields_);
    sqlForm->setContentsMargins(0, 0, 0, 0);
    sqlForm->addRow(tr("SQL &dialect:"), dialect_);
    sqlForm->addRow(tr("&Schema:"), schema_);
    tableValidation_ = new design::FieldValidation(table_, this);
    sqlForm->addRow(tr("&Table:"), tableValidation_);
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    auto* layout = new QVBoxLayout(this);
    auto* header = new QWidget(this);
    header->setFixedHeight(metrics.modalHeaderHeight);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(metrics.modalContentInset, 0, metrics.modalContentInset, 0);
    auto* heading = new design::Text(tr("Export results"), header);
    heading->setTypographyRole(design::TypographyRole::DialogTitle);
    headerLayout->addWidget(heading);
    headerLayout->addStretch();
    auto* dismiss = new design::Button({}, header);
    dismiss->setObjectName("exportDismiss");
    dismiss->setAccessibleName(tr("Close export"));
    dismiss->setVariant(design::ButtonVariant::Ghost);
    dismiss->setButtonSize(design::ButtonSize::IconSmall);
    dismiss->setDesignIcon(design::Icon::Close);
    headerLayout->addWidget(dismiss);
    connect(dismiss, &QPushButton::clicked, this, &ExportDialog::reject);
    layout->addWidget(header);
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    layout->addWidget(separator);
    auto* body = new QWidget(this);
    body->setProperty("designSurface", "panel");
    body->setAttribute(Qt::WA_StyledBackground);
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(metrics.modalContentInset, metrics.modalFooterInset,
                                   metrics.modalContentInset, metrics.modalFooterInset);
    bodyLayout->addWidget(scope_);
    setResultViewActive(false);
    bodyLayout->addLayout(form);
    bodyLayout->addWidget(sqlFields_);
    bodyLayout->addWidget(status_);
    bodyLayout->addStretch();
    layout->addWidget(body, 1);
    auto* footer = new QWidget(this);
    footer->setProperty("designSurface", "muted");
    footer->setAttribute(Qt::WA_StyledBackground);
    auto* buttons = new QHBoxLayout(footer);
    buttons->setContentsMargins(metrics.modalFooterInset, metrics.modalFooterVerticalInset,
                                metrics.modalFooterInset, metrics.modalFooterVerticalInset);
    buttons->addStretch();
    auto* close = new design::Button(tr("Close"), footer);
    close->setObjectName("exportClose");
    close->setVariant(design::ButtonVariant::Outline);
    close->setButtonSize(design::ButtonSize::Small);
    start_->setButtonSize(design::ButtonSize::Small);
    cancel_->setButtonSize(design::ButtonSize::Small);
    buttons->addWidget(close);
    buttons->addWidget(cancel_);
    buttons->addWidget(start_);
    layout->addWidget(footer);
    connect(format_, &QComboBox::currentIndexChanged, this, &ExportDialog::updateActions);
    connect(format_, &QComboBox::currentIndexChanged, this,
            [this] { formatValidation_->setError({}); });
    connect(destination_, &QLineEdit::textChanged, this, &ExportDialog::updateActions);
    connect(destination_, &QLineEdit::textChanged, this,
            [this] { destinationValidation_->setError({}); });
    connect(table_, &QLineEdit::textChanged, this, &ExportDialog::updateActions);
    connect(table_, &QLineEdit::textChanged, this, [this] { tableValidation_->setError({}); });
    connect(start_, &QPushButton::clicked, this, &ExportDialog::start);
    connect(cancel_, &QPushButton::clicked, this, &ExportDialog::cancel);
    connect(close, &QPushButton::clicked, this, &ExportDialog::reject);
    connect(browse_, &QPushButton::clicked, this, [this] {
        const auto path =
            QFileDialog::getSaveFileName(this, tr("Export destination"), destination_->text(), {},
                                         nullptr, QFileDialog::DontConfirmOverwrite);
        if (!path.isEmpty())
            destination_->setText(path);
    });
    connect(adapter, &EngineAdapter::eventReady, this, &ExportDialog::handleEvent);
    connect(adapter, &EngineAdapter::exportSubmissionFailed, this,
            [this](quint64 query, const QString& error) {
                if (submitting_ && query_ == query)
                    status_->setText(error);
            });
    updateActions();
}
ExportDialog::~ExportDialog() {
    if (export_ && adapter_)
        adapter_->cancelExport(*export_);
}
bool ExportDialog::isRunning() const {
    return submitting_ || export_.has_value();
}
void ExportDialog::setQuery(quint64 query) {
    if (query_ == query)
        return;
    clearQuery();
    query_ = query;
    if (!isRunning())
        status_->setText(tr("Choose a destination and export format."));
    updateActions();
}
void ExportDialog::setResultViewActive(bool active) {
    scope_->setText(active ? tr("Export the complete filtered and sorted result in its displayed "
                                "order without loading it into memory.")
                           : tr("Export the complete current result without loading it into "
                                "memory."));
    scope_->setObjectName("exportScope");
}
void ExportDialog::clearQuery() {
    query_.reset();
    if (isRunning()) {
        // Invalidate new submissions immediately, but retain the accepted
        // export identity until the engine acknowledges destination cleanup.
        closeAfter_ = true;
        cancel();
        return;
    }
    ++submissionToken_;
    status_->clear();
    hide();
    updateActions();
}
void ExportDialog::updateActions() {
    const bool sql = format_->currentData().toString() == "sql";
    const bool running = isRunning();
    sqlFields_->setVisible(sql);
    sqlFields_->setEnabled(!running);
    format_->setEnabled(!running);
    destination_->setEnabled(!running);
    browse_->setEnabled(!running);
    start_->setEnabled(adapter_ && query_ && !running && !destination_->text().isEmpty() &&
                       (!sql || !table_->text().isEmpty()));
    cancel_->setEnabled(running && !cancelling_);
}
void ExportDialog::start() {
    QStringList table;
    if (!schema_->text().isEmpty())
        table.append(schema_->text());
    if (!table_->text().isEmpty())
        table.append(table_->text());
    startExportToDialect(destination_->text(), format_->currentData().toString(), table,
                         dialect_->currentData().toString());
}
void ExportDialog::startExportTo(const QString& path, const QString& format,
                                 const QStringList& table, bool postgres) {
    startExportToDialect(path, format, table,
                         postgres ? QStringLiteral("postgres") : QStringLiteral("sqlite"));
}
void ExportDialog::startExportToDialect(const QString& path, const QString& format,
                                        const QStringList& table, const QString& dialect) {
    if (!adapter_ || !query_ || isRunning())
        return;
    if (path.isEmpty() || format_->findData(format) < 0 ||
        (format == "sql" && (table.isEmpty() || table.size() > 2 || table.last().isEmpty()))) {
        destinationValidation_->setError(path.isEmpty() ? tr("Choose a destination.") : QString{});
        formatValidation_->setError(format_->findData(format) < 0 ? tr("Choose a supported format.")
                                                                  : QString{});
        tableValidation_->setError(
            format == "sql" && (table.isEmpty() || table.size() > 2 || table.last().isEmpty())
                ? tr("Enter a table for SQL export.")
                : QString{});
        return;
    }
    const auto query = *query_;
    const auto token = ++submissionToken_;
    destination_->setText(path);
    format_->setCurrentIndex(format_->findData(format));
    schema_->setText(table.size() == 2 ? table.first() : QString{});
    table_->setText(table.isEmpty() ? QString{} : table.last());
    dialect_->setCurrentIndex(dialect_->findData(dialect));
    submitting_ = true;
    status_->clear();
    progressToast(this)->showProgress(tr("Export"), tr("Checking destination…"));
    start_->setText(tr("&Export"));
    updateActions();
    emit exportRunningChanged(true);
    if (!adapter_ || query_ != query || submissionToken_ != token || !submitting_)
        return;
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, query, token, path, format, table, dialect] {
                const bool exists = watcher->result();
                watcher->deleteLater();
                if (!adapter_ || query_ != query || submissionToken_ != token || !submitting_)
                    return;
                if (exists) {
                    ConfirmationDialog confirmation(
                        QMessageBox::Question, tr("Replace destination?"),
                        tr("Replace the existing file after export completes?\n%1").arg(path),
                        QMessageBox::Yes | QMessageBox::No, this);
                    confirmation.setTextFormat(Qt::PlainText);
                    confirmation.setDefaultButton(QMessageBox::No);
                    const auto answer = confirmation.exec();
                    // Confirmation dispatches events: never revive a cancelled or replaced request.
                    if (!adapter_ || query_ != query || submissionToken_ != token || !submitting_)
                        return;
                    if (answer != QMessageBox::Yes) {
                        finish(tr("Export cancelled."), false);
                        return;
                    }
                }
                status_->setText(tr("Starting export…"));
                progressToast(this)->showProgress(tr("Export"), tr("Starting export…"));
                const auto started =
                    adapter_->startExportDialect(query, path, format, table, dialect);
                if (query_ != query || submissionToken_ != token || !submitting_) {
                    if (started && adapter_)
                        adapter_->cancelExport(*started);
                    return;
                }
                export_ = started;
                exportQuery_ = started ? std::optional<quint64>(query) : std::nullopt;
                submitting_ = false;
                if (!export_) {
                    const auto message = status_->text() == tr("Starting export…")
                                             ? tr("Export could not be started.")
                                             : status_->text();
                    finish(message, true);
                    return;
                }
                updateActions();
            });
    watcher->setFuture(QtConcurrent::run([path] { return QFileInfo::exists(path); }));
}

void ExportDialog::cancel() {
    if (submitting_) {
        ++submissionToken_;
        finish(tr("Export cancelled."), false);
        return;
    }
    if (!export_ || cancelling_ || !adapter_)
        return;
    cancelling_ = true;
    status_->clear();
    progressToast(this)->showProgress(tr("Export"), tr("Cancelling…"));
    updateActions();
    adapter_->cancelExport(*export_);
}
void ExportDialog::handleEvent(const BridgeEvent& value) {
    if (!export_ || *export_ != value.id || !exportQuery_ || *exportQuery_ != value.query_id)
        return;
    const auto kind = text(value.kind);
    if (kind == "export_progress") {
        if (!cancelling_)
            progressToast(this)->showProgress(tr("Export"), tr("Exporting: %1 rows · %2 bytes")
                                                                .arg(value.exported_rows)
                                                                .arg(value.exported_bytes));
    } else if (kind == "export_finished") {
        finish(tr("Export complete: %1 rows · %2 bytes")
                   .arg(value.exported_rows)
                   .arg(value.exported_bytes),
               false);
    } else if (kind == "export_failed") {
        finish(text(value.error), true);
    }
}
void ExportDialog::finish(const QString& message, bool failed) {
    export_.reset();
    exportQuery_.reset();
    submitting_ = false;
    cancelling_ = false;
    clearProgressToast(this);
    status_->setText(message);
    start_->setText(failed ? tr("&Retry") : tr("&Export"));
    updateActions();
    if (closeAfter_) {
        closeAfter_ = false;
        hide();
    }
    emit exportRunningChanged(false);
}
void ExportDialog::showEvent(QShowEvent* event) {
    DialogShell::showEvent(event);
    layout()->setContentsMargins(0, 0, 0, 0);
    layout()->setSpacing(0);
}
void ExportDialog::closeEvent(QCloseEvent* event) {
    if (isRunning()) {
        closeAfter_ = true;
        cancel();
        event->ignore();
    } else {
        QDialog::closeEvent(event);
    }
}
void ExportDialog::reject() {
    if (isRunning()) {
        closeAfter_ = true;
        cancel();
    } else {
        QDialog::reject();
    }
}
} // namespace choscordb
