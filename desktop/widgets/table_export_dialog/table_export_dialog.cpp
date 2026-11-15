#include "widgets/table_export_dialog/table_export_dialog.h"
#include "bridge/rust_text.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/field/field.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "widgets/screen_state/screen_state.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace choscordb {
namespace {
using bridge_detail::fromRust;
}
TableExportDialog::TableExportDialog(EngineAdapter* adapter, quint64 connection,
                                     TableExportRequest request, QWidget* parent)
    : DialogShell(parent), adapter_(adapter), connection_(connection), base_(std::move(request)),
      format_(transferFormatChoices(EngineAdapter::tableExportFormats(), this)),
      csvFields_(new QWidget(this)), delimiter_(new QLineEdit(this)), quote_(new QLineEdit(this)),
      lineBreak_(codeLabelChoices(EngineAdapter::tableExportLineBreaks(), this)),
      nullText_(new QLineEdit(this)), header_(new QCheckBox(tr("Header row"), this)),
      sqlFields_(new QWidget(this)), structure_(new QCheckBox(tr("Structure"), this)),
      drop_(new QCheckBox(tr("DROP statement"), this)), data_(new QCheckBox(tr("Data"), this)),
      target_(new QComboBox(this)), path_(new QLineEdit(this)),
      browse_(new design::Button(tr("Browse…"), this)), patternRow_(new QWidget(this)),
      pattern_(new QLineEdit(this)), validation_(nullptr) {
    setObjectName("tableExportDialog");
    setWindowTitle(tr("Export tables"));
    setAppModal();
    resize(design::dialogInitialSize(design::DialogSize::Form));
    format_->setObjectName("tableExportFormat");
    delimiter_->setObjectName("tableExportDelimiter");
    quote_->setObjectName("tableExportQuote");
    lineBreak_->setObjectName("tableExportLineBreak");
    nullText_->setObjectName("tableExportNullText");
    header_->setObjectName("tableExportHeader");
    structure_->setObjectName("tableExportStructure");
    drop_->setObjectName("tableExportDrop");
    data_->setObjectName("tableExportData");
    target_->setObjectName("tableExportTarget");
    path_->setObjectName("tableExportPath");
    pattern_->setObjectName("tableExportPattern");
    browse_->setObjectName("tableExportBrowse");
    browse_->setVariant(design::ButtonVariant::Outline);
    target_->addItem(tr("Folder"), true);
    target_->addItem(tr("File"), false);
    const auto defaults = EngineAdapter::tableExportDefaults();
    format_->setCurrentIndex(format_->findData(defaults.format));
    delimiter_->setText(defaults.delimiter);
    quote_->setText(defaults.quote);
    lineBreak_->setCurrentIndex(lineBreak_->findData(defaults.lineBreak));
    nullText_->setText(defaults.nullText);
    header_->setChecked(defaults.header);
    structure_->setChecked(defaults.structure);
    drop_->setChecked(defaults.drop);
    data_->setChecked(defaults.data);
    target_->setCurrentIndex(target_->findData(defaults.folder));
    pattern_->setText(defaults.pattern);

    auto* sections = createSections();
    auto* heading = new design::Text(tr("Export tables"), sections->headerLayout()->parentWidget());
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss = sections->addDismissButton(tr("Close export"));
    connect(dismiss, &QPushButton::clicked, this, &TableExportDialog::reject);
    auto* bodyLayout = sections->bodyLayout();
    auto* body = bodyLayout->parentWidget();
    body->setProperty("designSurface", "panel");
    body->setAttribute(Qt::WA_StyledBackground);
    QStringList names;
    for (const auto& object : base_.objects)
        names << object.qualifiedName;
    auto* scope = createDescription(
        tr("%n table(s): %1", nullptr, int(base_.objects.size())).arg(names.join(", ")), body);
    scope->setObjectName("tableExportScope");
    scope->setWordWrap(true);
    bodyLayout->addWidget(scope);

    auto* form = sections->addForm();
    sections->addFormRow(form, tr("Format"), format_);
    auto* csvLayout = new QVBoxLayout(csvFields_);
    csvLayout->setContentsMargins(0, 0, 0, 0);
    auto* csvForm = sections->addForm(csvLayout);
    sections->addFormRow(csvForm, tr("Delimiter"), delimiter_);
    sections->addFormRow(csvForm, tr("Quote"), quote_);
    sections->addFormRow(csvForm, tr("Line break"), lineBreak_);
    sections->addFormRow(csvForm, tr("NULL text"), nullText_, design::FieldNeed::Optional);
    csvForm->addRow(QString(), header_);
    bodyLayout->addWidget(csvFields_);
    auto* sqlLayout = new QVBoxLayout(sqlFields_);
    sqlLayout->setContentsMargins(0, 0, 0, 0);
    auto* sqlForm = sections->addForm(sqlLayout);
    for (auto* option : {structure_, drop_, data_})
        sqlForm->addRow(QString(), option);
    bodyLayout->addWidget(sqlFields_);

    auto* destination = sections->addForm();
    sections->addFormRow(destination, tr("Save to"), target_);
    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(design::spacing(design::Spacing::Two));
    validation_ = new design::FieldValidation(path_, pathRow);
    pathLayout->addWidget(validation_, 1);
    pathLayout->addWidget(browse_, 0, Qt::AlignTop);
    browse_->setParent(pathRow);
    sections->addFormRow(destination, tr("Destination"), pathRow);
    auto* patternLayout = new QVBoxLayout(patternRow_);
    patternLayout->setContentsMargins(0, 0, 0, 0);
    auto* patternForm = sections->addForm(patternLayout);
    sections->addFormRow(patternForm, tr("File name"), pattern_);
    auto* patternHelp = createDescription(
        tr("Use {schema} and {table} in the file name. ChoscorDB adds the extension and "
           "replaces files with the same name."),
        patternRow_);
    patternForm->addRow(QString(), patternHelp);
    bodyLayout->addWidget(patternRow_);

    statusLine_ = sections->addStatusLine();
    statusLine_->setObjectName("tableExportStatusLine");
    cancel_ = sections->addAction(design::DialogActionRole::Dismiss, tr("Cancel"));
    cancel_->setObjectName("tableExportCancel");
    start_ = sections->addAction(design::DialogActionRole::Primary, tr("Export"));
    start_->setObjectName("tableExportStart");

    for (auto* box : {format_, target_})
        connect(box, &QComboBox::currentIndexChanged, this, &TableExportDialog::updateActions);
    for (auto* field : {delimiter_, quote_, nullText_, path_, pattern_})
        connect(field, &QLineEdit::textChanged, this, [this] { validation_->setError({}); });
    connect(start_, &QPushButton::clicked, this, &TableExportDialog::start);
    connect(cancel_, &QPushButton::clicked, this, &TableExportDialog::reject);
    connect(browse_, &QPushButton::clicked, this, [this] {
        const bool folder = target_->currentData().toBool();
        const auto path =
            folder ? QFileDialog::getExistingDirectory(this, tr("Export folder"), path_->text())
                   : QFileDialog::getSaveFileName(this, tr("Export file"), path_->text());
        if (!path.isEmpty())
            path_->setText(path);
    });
    connect(adapter, &EngineAdapter::eventReady, this, &TableExportDialog::handleEvent);
    updateActions();
}
TableExportDialog::~TableExportDialog() {
    if (export_ && adapter_)
        adapter_->cancelExport(*export_);
}
bool TableExportDialog::isRunning() const {
    return EngineAdapter::exportPresentation(state_).busy;
}
TableExportRequest TableExportDialog::request() const {
    auto request = base_;
    request.format = format_->currentData().toString();
    request.delimiter = delimiter_->text();
    request.quote = quote_->text();
    request.lineBreak = lineBreak_->currentData().toString();
    request.nullText = nullText_->text();
    request.header = header_->isChecked();
    request.structure = structure_->isChecked();
    request.drop = drop_->isChecked();
    request.data = data_->isChecked();
    request.folder = target_->currentData().toBool();
    request.path = path_->text();
    request.pattern = pattern_->text();
    return request;
}
void TableExportDialog::updateActions() {
    const bool running = isRunning();
    csvFields_->setVisible(format_->currentData(CsvOptionsRole).toBool());
    sqlFields_->setVisible(format_->currentData(SqlOptionsRole).toBool());
    patternRow_->setVisible(target_->currentData().toBool());
    for (QWidget* field :
         {static_cast<QWidget*>(format_), csvFields_, sqlFields_, static_cast<QWidget*>(target_),
          static_cast<QWidget*>(path_), static_cast<QWidget*>(browse_), patternRow_})
        field->setEnabled(!running);
    start_->setEnabled(adapter_ && !running);
    cancel_->setEnabled(state_ != ExportState::Cancelling);
}
void TableExportDialog::start() {
    if (!adapter_ || isRunning())
        return;
    const auto current = request();
    const auto error = EngineAdapter::tableExportError(current);
    if (!error.isEmpty()) {
        validation_->setError(error);
        return;
    }
    if (!apply(ExportEvent::Submit, tr("Starting export…")))
        return;
    QString submitError;
    export_ = adapter_->exportTables(connection_, current, &submitError);
    if (!export_) {
        settle(ExportEvent::Fail, submitError);
        return;
    }
    apply(ExportEvent::Start);
    updateActions();
}
void TableExportDialog::cancel() {
    if (!export_ || state_ == ExportState::Cancelling || !adapter_)
        return;
    apply(ExportEvent::Cancel);
    updateActions();
    adapter_->cancelExport(*export_);
}
void TableExportDialog::handleEvent(const BridgeEvent& value) {
    if (!export_ || *export_ != value.id)
        return;
    const auto kind = fromRust(value.kind);
    if (kind == "table_export_progress") {
        if (state_ == ExportState::Running)
            showState(tr("Exported %1 of %2 tables · %3")
                          .arg(value.exported_tables)
                          .arg(value.export_table_total)
                          .arg(tr("%n row(s)", nullptr, int(value.exported_rows))));
    } else if (kind == "table_export_finished") {
        settle(ExportEvent::Finish, tr("%n table(s)", nullptr, int(value.exported_tables)) +
                                        tr(" · ") +
                                        tr("%n row(s)", nullptr, int(value.exported_rows)));
    } else if (kind == "table_export_failed") {
        if (fromRust(value.error_kind) == "Cancelled") {
            if (state_ != ExportState::Cancelling)
                apply(ExportEvent::Cancel);
            settle(ExportEvent::CancelSettled, tr("Export cancelled."));
        } else {
            const auto table = fromRust(value.object);
            settle(ExportEvent::Fail,
                   table.isEmpty()
                       ? fromRust(value.error)
                       : tr("Could not export %1: %2").arg(table, fromRust(value.error)));
        }
    }
}
bool TableExportDialog::apply(ExportEvent event, const QString& detail) {
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
void TableExportDialog::showState(const QString& detail) {
    const auto shown = EngineAdapter::exportPresentation(state_, detail);
    statusLine_->setBusy(shown.busy);
    statusLine_->setState(statusLineState(shown.tone));
    statusLine_->setMessage(shown.message);
    statusLine_->setVisible(!shown.message.isEmpty());
}
void TableExportDialog::settle(ExportEvent event, const QString& detail) {
    export_.reset();
    if (!apply(event, detail))
        apply(ExportEvent::Fail, EngineAdapter::exportTransition(state_, event).error);
    start_->setText(state_ == ExportState::Failed ? tr("Retry") : tr("Export"));
    updateActions();
    if (state_ == ExportState::Succeeded) {
        closeAfter_ = false;
        closeWithSuccess(tr("Export complete"), detail);
        return;
    }
    if (closeAfter_) {
        closeAfter_ = false;
        if (state_ != ExportState::Failed)
            hide();
    }
}
void TableExportDialog::closeEvent(QCloseEvent* event) {
    if (isRunning()) {
        closeAfter_ = true;
        cancel();
        event->ignore();
    } else {
        QDialog::closeEvent(event);
    }
}
void TableExportDialog::reject() {
    if (isRunning()) {
        closeAfter_ = true;
        cancel();
    } else {
        DialogShell::reject();
    }
}
}
