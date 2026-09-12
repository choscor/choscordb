#include "widgets/export_dialog.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/theme.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
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
QString text(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
} // namespace
ExportDialog::ExportDialog(EngineAdapter* adapter, QWidget* parent)
    : DialogShell(parent), adapter_(adapter), format_(new QComboBox(this)),
      dialect_(new QComboBox(this)), destination_(new QLineEdit(this)),
      schema_(new QLineEdit(this)), table_(new QLineEdit(this)), sqlFields_(new QWidget(this)),
      browse_(new QPushButton(tr("&Browse…"), this)), start_(new QPushButton(tr("&Export"), this)),
      cancel_(new QPushButton(tr("Cancel export"), this)), status_(createInlineStatus(this)) {
    setObjectName("exportDialog");
    setWindowTitle(tr("Export results"));
    setModal(false);
    resize(design::dialogInitialSize(design::DialogSize::Short));
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
    dialect_->addItem(tr("SQLite"), false);
    dialect_->addItem(tr("PostgreSQL"), true);
    schema_->setPlaceholderText(tr("Optional"));
    table_->setToolTip(tr("One literal identifier; dots are not separators."));
    schema_->setToolTip(table_->toolTip());
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    auto* destinationRow = new QHBoxLayout;
    destinationRow->addWidget(destination_);
    destinationRow->addWidget(browse_);
    auto* form = new QFormLayout;
    form->addRow(tr("&Format:"), format_);
    auto* destinationLabel = new QLabel(tr("&Destination:"), this);
    destinationLabel->setBuddy(destination_);
    form->addRow(destinationLabel, destinationRow);
    auto* sqlForm = new QFormLayout(sqlFields_);
    sqlForm->setContentsMargins(0, 0, 0, 0);
    sqlForm->addRow(tr("SQL &dialect:"), dialect_);
    sqlForm->addRow(tr("&Schema:"), schema_);
    sqlForm->addRow(tr("&Table:"), table_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->addButton(start_, QDialogButtonBox::ActionRole);
    buttons->addButton(cancel_, QDialogButtonBox::ActionRole);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(createDescription(
        tr("Export the current result without loading the complete result into memory."), this));
    layout->addLayout(form);
    layout->addWidget(sqlFields_);
    layout->addWidget(status_);
    layout->addStretch();
    layout->addWidget(buttons);
    connect(format_, &QComboBox::currentIndexChanged, this, &ExportDialog::updateActions);
    connect(destination_, &QLineEdit::textChanged, this, &ExportDialog::updateActions);
    connect(table_, &QLineEdit::textChanged, this, &ExportDialog::updateActions);
    connect(start_, &QPushButton::clicked, this, &ExportDialog::start);
    connect(cancel_, &QPushButton::clicked, this, &ExportDialog::cancel);
    connect(buttons, &QDialogButtonBox::rejected, this, &ExportDialog::reject);
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
    status_->setText(tr("Choose a destination and export format."));
    updateActions();
}
void ExportDialog::clearQuery() {
    const bool running = isRunning();
    ++submissionToken_;
    if (export_ && adapter_)
        adapter_->cancelExport(*export_);
    query_.reset();
    export_.reset();
    submitting_ = cancelling_ = closeAfter_ = false;
    status_->clear();
    hide();
    updateActions();
    if (running)
        emit exportRunningChanged(false);
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
    startExportTo(destination_->text(), format_->currentData().toString(), table,
                  dialect_->currentData().toBool());
}
void ExportDialog::startExportTo(const QString& path, const QString& format,
                                 const QStringList& table, bool postgres) {
    if (!adapter_ || !query_ || isRunning())
        return;
    if (path.isEmpty() || format_->findData(format) < 0 ||
        (format == "sql" && (table.isEmpty() || table.size() > 2 || table.last().isEmpty()))) {
        status_->setText(
            tr("Choose a destination, a supported format, and a table for SQL export."));
        return;
    }
    const auto query = *query_;
    const auto token = ++submissionToken_;
    destination_->setText(path);
    format_->setCurrentIndex(format_->findData(format));
    schema_->setText(table.size() == 2 ? table.first() : QString{});
    table_->setText(table.isEmpty() ? QString{} : table.last());
    dialect_->setCurrentIndex(postgres ? 1 : 0);
    submitting_ = true;
    status_->setText(tr("Checking destination…"));
    start_->setText(tr("&Export"));
    updateActions();
    emit exportRunningChanged(true);
    if (!adapter_ || query_ != query || submissionToken_ != token || !submitting_)
        return;
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, query, token, path, format, table, postgres] {
                const bool exists = watcher->result();
                watcher->deleteLater();
                if (!adapter_ || query_ != query || submissionToken_ != token || !submitting_)
                    return;
                if (exists) {
                    QMessageBox confirmation(
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
                const auto started = adapter_->startExport(query, path, format, table, postgres);
                if (query_ != query || submissionToken_ != token || !submitting_) {
                    if (started && adapter_)
                        adapter_->cancelExport(*started);
                    return;
                }
                export_ = started;
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
    status_->setText(tr("Cancelling…"));
    updateActions();
    adapter_->cancelExport(*export_);
}
void ExportDialog::handleEvent(const BridgeEvent& value) {
    if (!export_ || *export_ != value.id || !query_ || *query_ != value.query_id)
        return;
    const auto kind = text(value.kind);
    if (kind == "export_progress") {
        if (!cancelling_)
            status_->setText(tr("Exporting: %1 rows · %2 bytes")
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
    submitting_ = false;
    cancelling_ = false;
    status_->setText(message);
    start_->setText(failed ? tr("&Retry") : tr("&Export"));
    updateActions();
    if (closeAfter_) {
        closeAfter_ = false;
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
} // namespace choscordb
