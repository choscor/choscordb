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
#include <QLocale>
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
      browse_(new design::Button(tr("Browse…"), this)), scope_(createDescription({}, this)) {
    browse_->setVariant(design::ButtonVariant::Outline);
    setObjectName("exportDialog");
    setWindowTitle(tr("Export results"));
    setAppModal();
    resize(design::dialogInitialSize(design::DialogSize::Form));
    format_->setObjectName("exportFormat");
    destination_->setObjectName("exportDestination");
    schema_->setObjectName("exportSchema");
    table_->setObjectName("exportTable");
    dialect_->setObjectName("exportDialect");
    format_->addItem(tr("CSV"), "csv");
    format_->addItem(tr("JSON"), "json");
    format_->addItem(tr("JSON Lines"), "jsonl");
    format_->addItem(tr("Excel"), "xlsx");
    format_->addItem(tr("SQL INSERT"), "sql");
    dialect_->addItem(tr("SQLite"), "sqlite");
    dialect_->addItem(tr("PostgreSQL"), "postgres");
    dialect_->addItem(tr("MySQL"), "mysql");
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
    bodyLayout->addWidget(scope_);
    setResultViewActive(false);
    auto* destinationRow = new QWidget(this);
    auto* destinationLayout = new QHBoxLayout(destinationRow);
    destinationLayout->setContentsMargins(0, 0, 0, 0);
    destinationLayout->setSpacing(design::spacing(design::Spacing::Two));
    destinationValidation_ = new design::FieldValidation(destination_, destinationRow);
    destinationLayout->addWidget(destinationValidation_, 1);
    destinationLayout->addWidget(browse_, 0, Qt::AlignTop);
    browse_->setParent(destinationRow);
    auto* form = sections->addForm();
    formatValidation_ = new design::FieldValidation(format_, this);
    sections->addFormRow(form, tr("Format"), formatValidation_);
    sections->addFormRow(form, tr("Destination"), destinationRow);
    auto* sqlLayout = new QVBoxLayout(sqlFields_);
    sqlLayout->setContentsMargins(0, 0, 0, 0);
    auto* sqlForm = sections->addForm(sqlLayout);
    sections->addFormRow(sqlForm, tr("SQL dialect"), dialect_);
    sections->addFormRow(sqlForm, tr("Schema"), schema_, design::FieldNeed::Optional);
    tableValidation_ = new design::FieldValidation(table_, this);
    sections->addFormRow(sqlForm, tr("Table"), tableValidation_);
    auto* identifierHelp = createDescription(
        tr("The schema and the table are each one identifier. A dot is part of the name."),
        sqlFields_);
    identifierHelp->setObjectName("exportIdentifierHelp");
    sqlForm->addRow(QString(), identifierHelp);
    bodyLayout->addWidget(sqlFields_);
    statusLine_ = sections->addStatusLine();
    statusLine_->setObjectName("exportStatusLine");
    status_ = statusLine_->findChild<QLabel*>("statusMessage");
    status_->setObjectName("exportStatus");
    cancel_ = sections->addAction(design::DialogActionRole::Dismiss, tr("Cancel"));
    cancel_->setObjectName("exportCancel");
    start_ = sections->addAction(design::DialogActionRole::Primary, tr("Export"));
    start_->setObjectName("exportStart");
    connect(format_, &QComboBox::currentIndexChanged, this, &ExportDialog::updateActions);
    connect(format_, &QComboBox::currentIndexChanged, this,
            [this] { formatValidation_->setError({}); });
    connect(destination_, &QLineEdit::textChanged, this, &ExportDialog::updateActions);
    connect(destination_, &QLineEdit::textChanged, this,
            [this] { destinationValidation_->setError({}); });
    connect(table_, &QLineEdit::textChanged, this, &ExportDialog::updateActions);
    connect(table_, &QLineEdit::textChanged, this, [this] { tableValidation_->setError({}); });
    connect(start_, &QPushButton::clicked, this, &ExportDialog::start);
    connect(cancel_, &QPushButton::clicked, this, &ExportDialog::reject);
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
    start_->setEnabled(adapter_ && query_ && !running);
    cancel_->setEnabled(state_ != ExportState::Cancelling);
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
    start_->setText(tr("Export"));
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
                const bool replace = ConfirmationDialog::confirm(
                    this, tr("Replace destination"),
                    tr("Replace the existing file after export completes?\n%1").arg(path),
                    tr("Replace"), ConfirmationDialog::Consequence::Destructive);
                if (!adapter_ || query_ != query || submissionToken_ != token ||
                    state_ != ExportState::Submitting)
                    return;
                if (!replace) {
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
                                              ? tr("Could not start the export.")
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
            showState(tr("Exporting: %1 · %2")
                          .arg(tr("%n row(s)", nullptr, int(value.exported_rows)),
                               QLocale().formattedDataSize(qint64(value.exported_bytes))));
    } else if (kind == "export_finished") {
        settle(ExportEvent::Finish,
               tr("%1 · %2 saved to %3")
                   .arg(tr("%n row(s)", nullptr, int(value.exported_rows)),
                        QLocale().formattedDataSize(qint64(value.exported_bytes)),
                        destination_->text()));
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
    submissionError_.clear();
    start_->setText(state_ == ExportState::Failed ? tr("Retry") : tr("Export"));
    updateActions();
    if (state_ == ExportState::Succeeded) {
        statusLine_->clearReport();
        closeAfter_ = false;
        emit exportRunningChanged(false);
        closeWithSuccess(tr("Export complete"), detail);
        return;
    }
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
        DialogShell::reject();
    }
}
}
