#include "widgets/export_dialog/export_dialog.h"
#include "bridge/engine_adapter.h"
#include "bridge/rust_text.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/field/field.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region.h"
#include "widgets/screen_state/screen_state.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
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
using bridge_detail::fromRust;
}
ExportDialog::ExportDialog(EngineAdapter* adapter, QWidget* parent)
    : DialogShell(parent), adapter_(adapter), format_(new QComboBox(this)),
      dialect_(new QComboBox(this)), destination_(new QLineEdit(this)),
      schema_(new QLineEdit(this)), table_(new QLineEdit(this)), sqlFields_(new QWidget(this)),
      browse_(new design::Button(tr("&Browse…"), this)),
      start_(new design::Button(tr("&Export"), this)),
      cancel_(new design::Button(tr("Cancel export"), this)), scope_(createDescription({}, this)) {
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
    auto* sections = createSections();
    auto* headerLayout = sections->headerLayout();
    auto* header = headerLayout->parentWidget();
    auto* heading = new design::Text(tr("Export results"), header);
    heading->setTextStyle(design::TextStyle::Headline);
    headerLayout->addWidget(heading);
    auto* dismiss = sections->addDismissButton(tr("Close export"));
    dismiss->setObjectName("exportDismiss");
    connect(dismiss, &QPushButton::clicked, this, &ExportDialog::reject);
    auto* bodyLayout = sections->bodyLayout();
    auto* body = bodyLayout->parentWidget();
    body->setProperty("designSurface", "panel");
    body->setAttribute(Qt::WA_StyledBackground);
    bodyLayout->setSpacing(design::spacing(design::Spacing::Two));
    bodyLayout->addWidget(scope_);
    setResultViewActive(false);
    bodyLayout->addLayout(form);
    bodyLayout->addWidget(sqlFields_);
    bodyLayout->addStretch();
    statusLine_ = new design::StatusLine(this);
    statusLine_->setObjectName("exportStatusLine");
    status_ = statusLine_->findChild<QLabel*>("statusMessage");
    status_->setObjectName("exportStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    statusLine_->hide();
    bodyLayout->addWidget(statusLine_);
    auto* buttons = sections->footerLayout();
    auto* footer = buttons->parentWidget();
    buttons->addStretch();
    auto* close = new design::Button(tr("Close"), footer);
    close->setObjectName("exportClose");
    close->setVariant(design::ButtonVariant::Outline);
    buttons->addWidget(close);
    buttons->addWidget(cancel_);
    buttons->addWidget(start_);
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
                if (state_ == ExportState::Submitting && query_ == query)
                    submissionError_ = error;
            });
    updateActions();
}
ExportDialog::~ExportDialog() {
    if (export_ && adapter_)
        adapter_->cancelExport(*export_);
}
bool ExportDialog::isRunning() const {
    return EngineAdapter::exportPresentation(state_).busy;
}
void ExportDialog::setQuery(quint64 query) {
    if (query_ == query)
        return;
    clearQuery();
    query_ = query;
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
        closeAfter_ = true;
        cancel();
        return;
    }
    ++submissionToken_;
    submissionError_.clear();
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
    cancel_->setEnabled(running && state_ != ExportState::Cancelling);
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
void ExportDialog::startExportToDialect(const QString& path, const QString& format,
                                        const QStringList& table, const QString& dialect) {
    if (!adapter_ || !query_ || isRunning())
        return;
    const auto tableError = format == "sql" ? EngineAdapter::sqlExportTableError(table) : QString{};
    if (path.isEmpty() || format_->findData(format) < 0 || !tableError.isEmpty()) {
        destinationValidation_->setError(path.isEmpty() ? tr("Choose a destination.") : QString{});
        formatValidation_->setError(format_->findData(format) < 0 ? tr("Choose a supported format.")
                                                                  : QString{});
        tableValidation_->setError(tableError);
        return;
    }
    const auto query = *query_;
    const auto token = ++submissionToken_;
    destination_->setText(path);
    format_->setCurrentIndex(format_->findData(format));
    schema_->setText(table.size() == 2 ? table.first() : QString{});
    table_->setText(table.isEmpty() ? QString{} : table.last());
    dialect_->setCurrentIndex(dialect_->findData(dialect));
    submissionError_.clear();
    apply(ExportEvent::Submit, tr("Checking destination…"));
    if (state_ != ExportState::Submitting)
        return;
    start_->setText(tr("&Export"));
    updateActions();
    emit exportRunningChanged(true);
    if (!adapter_ || query_ != query || submissionToken_ != token)
        return;
    auto* watcher = new QFutureWatcher<DocumentPathStatusDto>(this);
    connect(
        watcher, &QFutureWatcher<DocumentPathStatusDto>::finished, this,
        [this, watcher, query, token, path, format, table, dialect] {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (!adapter_ || query_ != query || submissionToken_ != token ||
                state_ != ExportState::Submitting)
                return;
            if (!result.error.empty()) {
                settle(ExportEvent::Fail,
                       tr("Could not check destination: %1").arg(fromRust(result.error)));
                return;
            }
            if (result.exists) {
                ConfirmationDialog confirmation(
                    QMessageBox::Question, tr("Replace destination?"),
                    tr("Replace the existing file after export completes?\n%1").arg(path),
                    QMessageBox::Yes | QMessageBox::No, this);
                confirmation.setTextFormat(Qt::PlainText);
                confirmation.setDefaultButton(QMessageBox::No);
                const auto answer = confirmation.exec();
                if (!adapter_ || query_ != query || submissionToken_ != token ||
                    state_ != ExportState::Submitting)
                    return;
                if (answer != QMessageBox::Yes) {
                    apply(ExportEvent::Cancel);
                    settle(ExportEvent::CancelSettled, tr("Export cancelled."));
                    return;
                }
            }
            submissionError_.clear();
            showState(tr("Starting export…"));
            const auto started = adapter_->startExportDialect(query, path, format, table, dialect);
            if (query_ != query || submissionToken_ != token || state_ != ExportState::Submitting) {
                if (started && adapter_)
                    adapter_->cancelExport(*started);
                return;
            }
            export_ = started;
            exportQuery_ = started ? std::optional<quint64>(query) : std::nullopt;
            if (!export_) {
                settle(ExportEvent::Fail, submissionError_.isEmpty()
                                              ? tr("Export could not be started.")
                                              : submissionError_);
                return;
            }
            apply(ExportEvent::Start);
            updateActions();
        });
    watcher->setFuture(QtConcurrent::run([path] {
        const auto bytes = path.toUtf8();
        return document_path_status(bridge_detail::utf8View(bytes));
    }));
}

void ExportDialog::cancel() {
    if (state_ == ExportState::Submitting) {
        ++submissionToken_;
        apply(ExportEvent::Cancel);
        settle(ExportEvent::CancelSettled, tr("Export cancelled."));
        return;
    }
    if (!export_ || state_ == ExportState::Cancelling || !adapter_)
        return;
    submissionError_.clear();
    apply(ExportEvent::Cancel);
    updateActions();
    adapter_->cancelExport(*export_);
}
void ExportDialog::handleEvent(const BridgeEvent& value) {
    if (!export_ || *export_ != value.id || !exportQuery_ || *exportQuery_ != value.query_id)
        return;
    const auto kind = fromRust(value.kind);
    if (kind == "export_progress") {
        if (state_ == ExportState::Running)
            showState(tr("Exporting: %1 rows · %2 bytes")
                          .arg(value.exported_rows)
                          .arg(value.exported_bytes));
    } else if (kind == "export_finished") {
        settle(ExportEvent::Finish, tr("Export complete: %1 rows · %2 bytes\n%3")
                                        .arg(value.exported_rows)
                                        .arg(value.exported_bytes)
                                        .arg(destination_->text()));
    } else if (kind == "export_failed") {
        if (fromRust(value.error_kind) == "Cancelled") {
            if (state_ != ExportState::Cancelling)
                apply(ExportEvent::Cancel);
            settle(ExportEvent::CancelSettled, tr("Export cancelled."));
        } else {
            settle(ExportEvent::Fail, fromRust(value.error));
        }
    }
}
bool ExportDialog::apply(ExportEvent event, const QString& detail) {
    const auto next = EngineAdapter::exportTransition(state_, event);
    if (!next.allowed) {
        statusLine_->setState(design::StatusLine::State::Error);
        statusLine_->setMessage(next.error);
        statusLine_->show();
        return false;
    }
    state_ = next.state;
    showState(detail);
    return true;
}
void ExportDialog::showState(const QString& detail) {
    const auto shown = EngineAdapter::exportPresentation(state_, detail);
    statusLine_->setBusy(shown.busy);
    statusLine_->setState(statusLineState(shown.tone));
    statusLine_->setMessage(shown.message);
    statusLine_->setVisible(!shown.message.isEmpty());
}
void ExportDialog::settle(ExportEvent event, const QString& detail) {
    export_.reset();
    exportQuery_.reset();
    if (!apply(event, detail))
        apply(ExportEvent::Fail, EngineAdapter::exportTransition(state_, event).error);
    auto* toast = state_ == ExportState::Succeeded ? windowToast(this) : nullptr;
    if (toast) {
        toast->showToast(tr("Success"), EngineAdapter::exportPresentation(state_, detail).message,
                         ToastVariant::Success);
        statusLine_->setMessage({});
        statusLine_->hide();
    }
    submissionError_.clear();
    start_->setText(state_ == ExportState::Failed ? tr("&Retry") : tr("&Export"));
    updateActions();
    if (closeAfter_) {
        closeAfter_ = false;
        if (state_ != ExportState::Failed)
            hide();
    }
    emit exportRunningChanged(false);
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
}
