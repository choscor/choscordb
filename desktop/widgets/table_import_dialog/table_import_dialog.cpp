#include "widgets/table_import_dialog/table_import_dialog.h"
#include "bridge/request_token.h"
#include "bridge/rust_text.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/field/field.h"
#include "design_system/status_line/status_line.h"
#include "design_system/table/table_style.h"
#include "design_system/text/text.h"
#include "widgets/screen_state/screen_state.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

namespace choscordb {
namespace {
using bridge_detail::fromRust;
}
TableImportDialog::TableImportDialog(EngineAdapter* adapter, quint64 connection,
                                     TableImportRequest request, const QString& targetName,
                                     QWidget* parent)
    : DialogShell(parent), adapter_(adapter), connection_(connection), base_(std::move(request)),
      path_(new QLineEdit(this)), browse_(new design::Button(tr("Browse…"), this)),
      format_(transferFormatChoices(EngineAdapter::tableImportFormats(), this)),
      encoding_(codeLabelChoices(EngineAdapter::tableImportEncodings(), this)),
      delimiter_(new QLineEdit(this)), quote_(new QLineEdit(this)), nullText_(new QLineEdit(this)),
      header_(new QCheckBox(tr("Header row"), this)), tableName_(new QLineEdit(this)),
      conflict_(codeLabelChoices(EngineAdapter::tableImportConflicts(base_.driver), this)),
      previewModel_(new QStandardItemModel(this)), preview_(new QTableView(this)),
      mappingFields_(new QWidget(this)), sqlPreview_(new QPlainTextEdit(this)) {
    setObjectName("tableImportDialog");
    setWindowTitle(tr("Import data"));
    setAppModal();
    resize(design::dialogInitialSize(design::DialogSize::Profiles));
    path_->setObjectName("tableImportPath");
    browse_->setObjectName("tableImportBrowse");
    browse_->setVariant(design::ButtonVariant::Outline);
    format_->setObjectName("tableImportFormat");
    encoding_->setObjectName("tableImportEncoding");
    delimiter_->setObjectName("tableImportDelimiter");
    quote_->setObjectName("tableImportQuote");
    nullText_->setObjectName("tableImportNullText");
    header_->setObjectName("tableImportHeader");
    tableName_->setObjectName("tableImportTableName");
    conflict_->setObjectName("tableImportConflict");
    preview_->setObjectName("tableImportPreview");
    mappingFields_->setObjectName("tableImportMapping");
    const auto defaults = EngineAdapter::tableImportDefaults();
    format_->setCurrentIndex(format_->findData(defaults.format));
    encoding_->setCurrentIndex(encoding_->findData(defaults.encoding));
    delimiter_->setText(defaults.delimiter);
    quote_->setText(defaults.quote);
    nullText_->setText(defaults.nullText);
    header_->setChecked(defaults.header);
    conflict_->setCurrentIndex(conflict_->findData(defaults.conflict));

    auto* sections = createSections();
    auto* heading = new design::Text(tr("Import data"), sections->headerLayout()->parentWidget());
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss = sections->addDismissButton(tr("Close import"));
    connect(dismiss, &QPushButton::clicked, this, &TableImportDialog::reject);
    auto* bodyLayout = sections->bodyLayout();
    auto* body = bodyLayout->parentWidget();
    body->setProperty("designSurface", "panel");
    body->setAttribute(Qt::WA_StyledBackground);
    auto* scope = createDescription(base_.existing
                                        ? tr("Import rows into %1.").arg(targetName)
                                        : tr("Import rows into a new table in %1.").arg(targetName),
                                    body);
    scope->setObjectName("tableImportScope");
    scope->setWordWrap(true);
    bodyLayout->addWidget(scope);

    auto* split = new QHBoxLayout();
    split->setSpacing(design::spacing(design::Spacing::Four));
    auto* settings = new QVBoxLayout();
    split->addLayout(settings, 2);
    auto* review = new QVBoxLayout();
    review->setSpacing(design::spacing(design::Spacing::Two));
    split->addLayout(review, 3);
    bodyLayout->addLayout(split, 1);

    sourceForm_ = sections->addForm(settings);
    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(design::spacing(design::Spacing::Two));
    validation_ = new design::FieldValidation(path_, pathRow);
    pathLayout->addWidget(validation_, 1);
    pathLayout->addWidget(browse_, 0, Qt::AlignTop);
    browse_->setParent(pathRow);
    sections->addFormRow(sourceForm_, tr("File"), pathRow);
    sections->addFormRow(sourceForm_, tr("Format"), format_);
    sections->addFormRow(sourceForm_, tr("Encoding"), encoding_);
    sections->addFormRow(sourceForm_, tr("Delimiter"), delimiter_);
    sections->addFormRow(sourceForm_, tr("Quote"), quote_);
    sections->addFormRow(sourceForm_, tr("NULL text"), nullText_, design::FieldNeed::Optional);
    sourceForm_->addRow(QString(), header_);
    sections->addFormRow(sourceForm_, tr("New table"), tableName_);
    sourceForm_->setRowVisible(tableName_, !base_.existing);
    sections->addFormRow(sourceForm_, tr("On conflict"), conflict_);
    sourceForm_->setRowVisible(conflict_, base_.existing);
    settings->addStretch(1);

    previewSummary_ = createDescription(QString(), body);
    previewSummary_->setObjectName("tableImportPreviewSummary");
    review->addWidget(previewSummary_);
    preview_->setModel(previewModel_);
    preview_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    preview_->setWordWrap(false);
    preview_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    design::configureDataTable(*preview_);
    preview_->setMinimumHeight(preview_->horizontalHeader()->sizeHint().height() +
                               3 * preview_->verticalHeader()->defaultSectionSize() +
                               2 * preview_->frameWidth());
    review->addWidget(preview_, 2);

    review->addWidget(createDescription(
        base_.existing ? tr("Choose the file column for each table column.")
                       : tr("Choose the file columns and the names of the new columns."),
        body));
    auto* mappingScroll = new QScrollArea(this);
    mappingScroll->setObjectName("tableImportMappingScroll");
    mappingScroll->setFrameShape(QFrame::NoFrame);
    mappingScroll->setWidgetResizable(true);
    mappingScroll->viewport()->setAutoFillBackground(false);
    mappingScroll->setWidget(mappingFields_);
    mappingFields_->setAutoFillBackground(false);
    mappingForm_ = new QFormLayout(mappingFields_);
    design::DialogSections::configureForm(mappingForm_);
    review->addWidget(mappingScroll, 1);

    review->addWidget(createDescription(tr("SQL to execute for each row"), body));
    sqlPreview_->setObjectName("tableImportSql");
    sqlPreview_->setAccessibleName(tr("SQL preview"));
    sqlPreview_->setReadOnly(true);
    sqlPreview_->setProperty("designRole", "codePreview");
    sqlPreview_->setFont(
        design::resolveTypography(design::TextStyle::Body, design::FontDesign::Monospaced));
    sqlPreview_->setFrameShape(QFrame::NoFrame);
    review->addWidget(sqlPreview_, 1);

    statusLine_ = sections->addStatusLine();
    statusLine_->setObjectName("tableImportStatusLine");
    cancel_ = sections->addAction(design::DialogActionRole::Dismiss, tr("Cancel"));
    cancel_->setObjectName("tableImportCancel");
    start_ = sections->addAction(design::DialogActionRole::Primary, tr("Import"));
    start_->setObjectName("tableImportStart");

    previewTimer_.setSingleShot(true);
    previewTimer_.setInterval(EngineAdapter::filterDelayMs());
    connect(&previewTimer_, &QTimer::timeout, this, &TableImportDialog::refreshPreview);
    const auto schedulePreview = [this] {
        validation_->setError({});
        previewTimer_.start();
    };
    for (auto* field : {path_, delimiter_, quote_, nullText_})
        connect(field, &QLineEdit::textChanged, this, schedulePreview);
    for (auto* box : {format_, encoding_})
        connect(box, &QComboBox::currentIndexChanged, this, schedulePreview);
    connect(header_, &QCheckBox::toggled, this, schedulePreview);
    connect(tableName_, &QLineEdit::textChanged, this, [this] {
        validation_->setError({});
        updateReview();
    });
    connect(conflict_, &QComboBox::currentIndexChanged, this, &TableImportDialog::updateReview);
    connect(start_, &QPushButton::clicked, this, &TableImportDialog::start);
    connect(cancel_, &QPushButton::clicked, this, &TableImportDialog::reject);
    connect(browse_, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Import file"), path_->text());
        if (!path.isEmpty())
            path_->setText(path);
    });
    connect(adapter, &EngineAdapter::eventReady, this, &TableImportDialog::handleEvent);
    if (base_.existing) {
        targetToken_ = nextRequestToken();
        if (!adapter->inspectEditTarget(connection_, base_.objectId, targetToken_))
            targetError_ = tr("The table columns could not be loaded.");
    }
    refreshPreview();
}
TableImportDialog::~TableImportDialog() {
    if (import_ && adapter_)
        adapter_->cancelExport(*import_);
}
bool TableImportDialog::isRunning() const {
    return EngineAdapter::importPresentation(state_).busy;
}
TableImportRequest TableImportDialog::request() const {
    auto request = base_;
    request.path = path_->text();
    request.format = format_->currentData().toString();
    request.encoding = encoding_->currentData().toString();
    request.delimiter = delimiter_->text();
    request.quote = quote_->text();
    request.nullText = nullText_->text();
    request.header = header_->isChecked();
    request.tableName = tableName_->text();
    request.conflict = conflict_->currentData().toString();
    request.mapping.clear();
    for (qsizetype index = 0; index < mapping_.size(); ++index) {
        const auto& row = mapping_.at(index);
        if (row.source)
            request.mapping.push_back({row.target, row.source->currentData().toInt()});
        else
            request.mapping.push_back(
                {row.name->text(), row.include->isChecked() ? int(index) : -1});
    }
    return request;
}
void TableImportDialog::refreshPreview() {
    previewTimer_.stop();
    previewModel_->clear();
    fileColumns_.clear();
    if (path_->text().isEmpty()) {
        previewSummary_->setText(tr("Choose a CSV or JSON file to see a preview."));
        rebuildMapping();
        updateActions();
        return;
    }
    const auto preview = EngineAdapter::tableImportPreview(request());
    if (!preview.valid) {
        previewSummary_->setText(QString());
        validation_->setError(preview.error);
        rebuildMapping();
        updateActions();
        return;
    }
    fileColumns_ = preview.columns;
    previewModel_->setColumnCount(int(fileColumns_.size()));
    previewModel_->setHorizontalHeaderLabels(fileColumns_);
    previewModel_->setRowCount(preview.rowCount);
    const auto width = fileColumns_.size();
    for (qsizetype cell = 0; width > 0 && cell < preview.cells.size(); ++cell) {
        auto* item =
            new QStandardItem(preview.nulls.value(cell) ? tr("NULL") : preview.cells.at(cell));
        item->setData(preview.nulls.value(cell), design::CellNullRole);
        previewModel_->setItem(int(cell / width), int(cell % width), item);
    }
    design::fitColumnsToContents(*preview_);
    previewSummary_->setText(tr("Preview of the first %n row(s).", nullptr, preview.rowCount));
    rebuildMapping();
    updateActions();
}
void TableImportDialog::rebuildMapping() {
    while (mappingForm_->rowCount() > 0)
        mappingForm_->removeRow(0);
    mapping_.clear();
    if (base_.existing) {
        const auto sources = EngineAdapter::tableImportDefaultMapping(tableColumns_, fileColumns_);
        for (qsizetype index = 0; index < tableColumns_.size(); ++index) {
            MappingRow row;
            row.target = tableColumns_.at(index);
            row.source = new QComboBox(mappingFields_);
            row.source->setObjectName(QStringLiteral("tableImportSource_") + row.target);
            row.source->addItem(tr("Do not import"), -1);
            for (qsizetype column = 0; column < fileColumns_.size(); ++column)
                row.source->addItem(fileColumns_.at(column), int(column));
            row.source->setCurrentIndex(row.source->findData(sources.value(index, -1)));
            connect(row.source, &QComboBox::currentIndexChanged, this,
                    &TableImportDialog::updateReview);
            mappingForm_->addRow(row.target, row.source);
            mapping_.push_back(row);
        }
        updateReview();
        return;
    }
    for (qsizetype index = 0; index < fileColumns_.size(); ++index) {
        MappingRow row;
        row.include = new QCheckBox(fileColumns_.at(index), mappingFields_);
        row.include->setObjectName(QStringLiteral("tableImportInclude_%1").arg(index));
        row.include->setChecked(true);
        row.name = new QLineEdit(fileColumns_.at(index), mappingFields_);
        row.name->setObjectName(QStringLiteral("tableImportColumnName_%1").arg(index));
        row.name->setAccessibleName(tr("New column name for %1").arg(fileColumns_.at(index)));
        connect(row.include, &QCheckBox::toggled, row.name, &QWidget::setEnabled);
        connect(row.include, &QCheckBox::toggled, this, &TableImportDialog::updateReview);
        connect(row.name, &QLineEdit::textChanged, this, &TableImportDialog::updateReview);
        mappingForm_->addRow(row.include, row.name);
        mapping_.push_back(row);
    }
    updateReview();
}
void TableImportDialog::updateReview() {
    const auto review = EngineAdapter::tableImportReview(request(), qualifiedName_, tableColumns_,
                                                         keyColumns_, fileColumns_);
    sqlPreview_->setPlainText(review.valid ? review.sql : review.error);
}
void TableImportDialog::updateActions() {
    const bool running = isRunning();
    const bool csv = format_->currentData(CsvOptionsRole).toBool();
    for (QWidget* field : {static_cast<QWidget*>(delimiter_), static_cast<QWidget*>(quote_),
                           static_cast<QWidget*>(nullText_), static_cast<QWidget*>(header_)})
        sourceForm_->setRowVisible(field, csv);
    for (QWidget* field :
         {static_cast<QWidget*>(path_), static_cast<QWidget*>(browse_),
          static_cast<QWidget*>(format_), static_cast<QWidget*>(encoding_),
          static_cast<QWidget*>(delimiter_), static_cast<QWidget*>(quote_),
          static_cast<QWidget*>(nullText_), static_cast<QWidget*>(header_),
          static_cast<QWidget*>(tableName_), static_cast<QWidget*>(conflict_), mappingFields_})
        field->setEnabled(!running);
    const bool targetReady = !base_.existing || !tableColumns_.isEmpty();
    start_->setEnabled(adapter_ && !running && targetReady && !fileColumns_.isEmpty());
    cancel_->setEnabled(state_ != ExportState::Cancelling);
}
void TableImportDialog::start() {
    if (!adapter_ || isRunning())
        return;
    if (!targetError_.isEmpty()) {
        validation_->setError(targetError_);
        return;
    }
    const auto current = request();
    const auto error = EngineAdapter::tableImportError(current);
    if (!error.isEmpty()) {
        validation_->setError(error);
        return;
    }
    if (!apply(ExportEvent::Submit, tr("Reading the file…")))
        return;
    QString submitError;
    import_ = adapter_->importTable(connection_, current, &submitError);
    if (!import_) {
        settle(ExportEvent::Fail, submitError);
        return;
    }
    apply(ExportEvent::Start, tr("Reading the file…"));
    updateActions();
}
void TableImportDialog::cancel() {
    if (!import_ || state_ == ExportState::Cancelling || !adapter_)
        return;
    apply(ExportEvent::Cancel);
    updateActions();
    adapter_->cancelExport(*import_);
}
void TableImportDialog::handleEvent(const BridgeEvent& value) {
    const auto kind = fromRust(value.kind);
    if (targetToken_ != 0 && value.request_token == targetToken_ && value.id == connection_ &&
        (kind == "edit_target" || kind == "edit_target_failed")) {
        targetToken_ = 0;
        tableColumns_.clear();
        if (kind == "edit_target_failed") {
            targetError_ = fromRust(value.error);
            validation_->setError(targetError_);
        } else {
            qualifiedName_ = fromRust(value.edit_target.qualified_name);
            keyColumns_ = bridge_detail::fromRustStrings(value.edit_target.key_columns);
            for (const auto& column : value.edit_target.columns)
                if (!column.generated)
                    tableColumns_.push_back(fromRust(column.name));
        }
        rebuildMapping();
        updateActions();
        return;
    }
    if (!import_ || *import_ != value.id)
        return;
    if (kind == "table_import_progress") {
        if (state_ == ExportState::Running)
            showState(tr("Writing %n row(s)…", nullptr, int(value.exported_rows)));
    } else if (kind == "table_import_finished") {
        emit imported(connection_);
        settle(ExportEvent::Finish, tr("Imported %n row(s).", nullptr, int(value.exported_rows)));
    } else if (kind == "table_import_failed") {
        if (fromRust(value.error_kind) == "Cancelled") {
            if (state_ != ExportState::Cancelling)
                apply(ExportEvent::Cancel);
            settle(ExportEvent::CancelSettled);
        } else {
            settle(ExportEvent::Fail, fromRust(value.error));
        }
    }
}
bool TableImportDialog::apply(ExportEvent event, const QString& detail) {
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
void TableImportDialog::showState(const QString& detail) {
    const auto shown = EngineAdapter::importPresentation(state_, detail);
    statusLine_->setBusy(shown.busy);
    statusLine_->setState(statusLineState(shown.tone));
    statusLine_->setMessage(shown.message);
    statusLine_->setVisible(!shown.message.isEmpty());
}
void TableImportDialog::settle(ExportEvent event, const QString& detail) {
    import_.reset();
    if (!apply(event, detail))
        apply(ExportEvent::Fail, EngineAdapter::exportTransition(state_, event).error);
    start_->setText(state_ == ExportState::Failed ? tr("Retry") : tr("Import"));
    updateActions();
    if (state_ == ExportState::Succeeded) {
        closeAfter_ = false;
        closeWithSuccess(tr("Import complete"), detail);
        return;
    }
    if (closeAfter_) {
        closeAfter_ = false;
        if (state_ != ExportState::Failed)
            hide();
    }
}
void TableImportDialog::closeEvent(QCloseEvent* event) {
    if (isRunning()) {
        closeAfter_ = true;
        cancel();
        event->ignore();
    } else {
        QDialog::closeEvent(event);
    }
}
void TableImportDialog::reject() {
    if (isRunning()) {
        closeAfter_ = true;
        cancel();
    } else {
        DialogShell::reject();
    }
}
}
