#include "widgets/structure_editor_dialog/structure_editor_dialog.h"
#include "bridge/request_token.h"
#include "bridge/rust_text.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "widgets/screen_state/screen_state.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace choscordb {
namespace {
using bridge_detail::fromRust;
}
StructureEditorDialog::StructureEditorDialog(EngineAdapter* adapter, quint64 connection,
                                             StructureTable table, const QString& objectId,
                                             const QString& targetName, QWidget* parent)
    : DialogShell(parent), adapter_(adapter), connection_(connection), base_(std::move(table)),
      kinds_(EngineAdapter::structureChangeKinds(base_.driver, !base_.existing)),
      types_(EngineAdapter::structureTypes(base_.driver)) {
    setObjectName("structureEditorDialog");
    const auto title = base_.existing ? tr("Edit structure") : tr("Design new table");
    setWindowTitle(title);
    setAppModal();
    resize(design::dialogInitialSize(design::DialogSize::Detail));

    auto* sections = createSections();
    auto* heading = new design::Text(title, sections->headerLayout()->parentWidget());
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss = sections->addDismissButton(tr("Close structure editor"));
    connect(dismiss, &QPushButton::clicked, this, &StructureEditorDialog::reject);
    auto* bodyLayout = sections->bodyLayout();
    auto* body = bodyLayout->parentWidget();
    body->setProperty("designSurface", "panel");
    body->setAttribute(Qt::WA_StyledBackground);
    auto* scope = createDescription(
        base_.existing ? tr("Change the columns, indexes and keys of %1.").arg(targetName)
                       : tr("Design a new table in %1.").arg(targetName),
        body);
    scope->setObjectName("structureEditorScope");
    scope->setWordWrap(true);
    bodyLayout->addWidget(scope);
    if (!base_.existing) {
        auto* form = sections->addForm();
        tableName_ = new QLineEdit(this);
        tableName_->setObjectName("structureEditorTableName");
        sections->addFormRow(form, tr("Table name"), tableName_);
        connect(tableName_, &QLineEdit::textChanged, &reviewTimer_, qOverload<>(&QTimer::start));
    }

    add_ = new design::Button(tr("Add change"), body);
    add_->setObjectName("structureEditorAdd");
    add_->setVariant(design::ButtonVariant::Outline);
    add_->setDesignIcon(design::Icon::Add);
    auto* menu = new QMenu(add_);
    menu->setToolTipsVisible(true);
    add_->setMenu(menu);
    for (const auto& kind : kinds_) {
        auto* action = menu->addAction(kind.label);
        action->setObjectName(QStringLiteral("structureEditorAdd_") + kind.code);
        action->setEnabled(kind.unavailable.isEmpty());
        action->setToolTip(kind.unavailable);
        connect(action, &QAction::triggered, this, [this, kind] { addChange(kind); });
    }
    bodyLayout->addWidget(add_, 0, Qt::AlignLeft);

    changeList_ = new QWidget(this);
    changeList_->setObjectName("structureEditorChanges");
    changeList_->setAutoFillBackground(false);
    changeLayout_ = new QVBoxLayout(changeList_);
    changeLayout_->setContentsMargins(0, 0, 0, 0);
    changeLayout_->setSpacing(design::spacing(design::Spacing::Three));
    empty_ = createDescription(tr("Add a change to see the SQL."), changeList_);
    empty_->setObjectName("structureEditorEmpty");
    changeLayout_->addWidget(empty_);
    changeLayout_->addStretch(1);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("structureEditorScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setWidget(changeList_);
    bodyLayout->addWidget(scroll, 1);

    error_ = new QLabel(body);
    error_->setObjectName("structureEditorError");
    error_->setProperty("designRole", "fieldError");
    error_->setWordWrap(true);
    error_->hide();
    bodyLayout->addWidget(error_);
    bodyLayout->addWidget(createDescription(tr("SQL to execute"), body));
    sqlPreview_ = new QPlainTextEdit(body);
    sqlPreview_->setObjectName("structureEditorSql");
    sqlPreview_->setAccessibleName(tr("SQL preview"));
    sqlPreview_->setReadOnly(true);
    sqlPreview_->setProperty("designRole", "codePreview");
    sqlPreview_->setFont(
        design::resolveTypography(design::TextStyle::Body, design::FontDesign::Monospaced));
    sqlPreview_->setFrameShape(QFrame::NoFrame);
    bodyLayout->addWidget(sqlPreview_);

    statusLine_ = sections->addStatusLine();
    statusLine_->setObjectName("structureEditorStatusLine");
    cancel_ = sections->addAction(design::DialogActionRole::Dismiss, tr("Cancel"));
    cancel_->setObjectName("structureEditorCancel");
    apply_ = sections->addAction(design::DialogActionRole::Primary, tr("Apply"));
    apply_->setObjectName("structureEditorApply");
    connect(cancel_, &QPushButton::clicked, this, &StructureEditorDialog::reject);
    connect(apply_, &QPushButton::clicked, this, &StructureEditorDialog::apply);

    reviewTimer_.setSingleShot(true);
    reviewTimer_.setInterval(EngineAdapter::filterDelayMs());
    connect(&reviewTimer_, &QTimer::timeout, this, &StructureEditorDialog::updateReview);
    connect(adapter, &EngineAdapter::eventReady, this, &StructureEditorDialog::handleEvent);
    connect(adapter, &EngineAdapter::objectInspectionReady, this,
            [this](quint64 connection, const QString&, quint64 token,
                   const ObjectInspection& inspection) {
                handleInspection(connection, token, inspection);
            });
    connect(adapter, &EngineAdapter::objectInspectionFailed, this,
            [this](quint64 connection, const QString&, quint64 token, const QString& error) {
                if (connection == connection_ && token != 0 &&
                    (token == indexesToken_ || token == keysToken_))
                    failLoad(error);
            });
    if (base_.existing) {
        targetToken_ = nextRequestToken();
        indexesToken_ = nextRequestToken();
        keysToken_ = nextRequestToken();
        showStatus(tr("Loading the table structure…"), false, true);
        if (!adapter->inspectEditTarget(connection_, objectId, targetToken_)) {
            failLoad(tr("The table columns could not be loaded."));
        } else {
            adapter->loadObjectInspection(connection_, objectId, ObjectInspectionPane::Indexes,
                                          indexesToken_);
            adapter->loadObjectInspection(connection_, objectId, ObjectInspectionPane::Keys,
                                          keysToken_);
        }
    }
    updateReview();
}
StructureTable StructureEditorDialog::table() const {
    auto table = base_;
    if (tableName_)
        table.tableName = tableName_->text();
    return table;
}
QList<StructureChange> StructureEditorDialog::changes() const {
    QList<StructureChange> result;
    for (const auto& editor : editors_) {
        StructureChange change{editor.kind, {}};
        for (const auto& field : editor.fields) {
            QString value;
            if (auto* line = qobject_cast<QLineEdit*>(field.widget))
                value = line->text();
            else if (auto* check = qobject_cast<QCheckBox*>(field.widget))
                value = check->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
            else if (auto* box = qobject_cast<QComboBox*>(field.widget))
                value = box->isEditable() || !box->currentData().isValid()
                            ? box->currentText()
                            : box->currentData().toString();
            change.values.push_back({field.key, value});
        }
        result.push_back(std::move(change));
    }
    return result;
}
QWidget* StructureEditorDialog::fieldWidget(const StructureField& field, QWidget* parent) {
    if (field.input == QStringLiteral("flag")) {
        auto* check = new QCheckBox(field.label, parent);
        connect(check, &QCheckBox::toggled, this, &StructureEditorDialog::updateReview);
        return check;
    }
    if (field.input == QStringLiteral("text")) {
        auto* line = new QLineEdit(parent);
        connect(line, &QLineEdit::textChanged, &reviewTimer_, qOverload<>(&QTimer::start));
        return line;
    }
    QStringList names;
    if (field.names == QStringLiteral("table_columns")) {
        for (const auto& column : base_.columns)
            names.push_back(column.name);
    } else if (field.names == QStringLiteral("table_indexes")) {
        names = base_.indexes;
    } else if (field.names == QStringLiteral("table_keys")) {
        for (const auto& key : base_.keys)
            names.push_back(key.name);
    }
    auto* box =
        field.choices.isEmpty() ? new QComboBox(parent) : codeLabelChoices(field.choices, parent);
    box->addItems(names);
    if (field.input == QStringLiteral("editable_choice")) {
        box->setEditable(true);
        box->addItems(types_);
        box->setCurrentIndex(field.required ? 0 : -1);
        connect(box, &QComboBox::editTextChanged, &reviewTimer_, qOverload<>(&QTimer::start));
    }
    connect(box, &QComboBox::currentIndexChanged, this, &StructureEditorDialog::updateReview);
    return box;
}
void StructureEditorDialog::addChange(const StructureChangeKindInfo& kind) {
    if (isApplying() || !kind.unavailable.isEmpty())
        return;
    ChangeEditor editor;
    editor.kind = kind.code;
    editor.frame = new QWidget(changeList_);
    editor.frame->setObjectName(QStringLiteral("structureChange_%1").arg(editors_.size()));
    auto* layout = new QVBoxLayout(editor.frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(design::spacing(design::Spacing::One));
    auto* header = new QHBoxLayout();
    editor.title = new design::Text(kind.label, editor.frame);
    editor.title->setObjectName("structureChangeTitle");
    editor.title->setSectionLabel();
    header->addWidget(editor.title, 1);
    auto* remove = new design::Button(QString(), editor.frame);
    remove->setObjectName("structureChangeRemove");
    remove->setVariant(design::ButtonVariant::Ghost);
    remove->setButtonSize(design::ButtonSize::IconSmall);
    remove->setDesignIcon(design::Icon::Trash);
    remove->setAccessibleName(tr("Remove change"));
    remove->setToolTip(tr("Remove change"));
    header->addWidget(remove);
    layout->addLayout(header);
    auto* form = new QFormLayout();
    design::DialogSections::configureForm(form);
    for (const auto& field : kind.fields) {
        auto* widget = fieldWidget(field, editor.frame);
        widget->setObjectName(QStringLiteral("structureField_") + field.key);
        widget->setAccessibleName(field.label);
        editor.fields.push_back({field.key, widget});
        if (field.input == QStringLiteral("flag"))
            form->addRow(QString(), widget);
        else
            form->addRow(field.label, widget);
    }
    layout->addLayout(form);
    connect(remove, &QPushButton::clicked, this,
            [this, frame = editor.frame] { removeChange(frame); });
    changeLayout_->insertWidget(changeLayout_->count() - 1, editor.frame);
    editors_.push_back(editor);
    if (!editor.fields.isEmpty())
        editor.fields.first().widget->setFocus();
    updateReview();
}
void StructureEditorDialog::removeChange(QWidget* frame) {
    if (isApplying())
        return;
    for (qsizetype index = 0; index < editors_.size(); ++index) {
        if (editors_.at(index).frame != frame)
            continue;
        editors_.removeAt(index);
        frame->deleteLater();
        break;
    }
    updateReview();
}
void StructureEditorDialog::updateReview() {
    reviewTimer_.stop();
    empty_->setVisible(editors_.isEmpty());
    const auto plan = EngineAdapter::structurePlan(table(), changes());
    for (qsizetype index = 0; index < editors_.size(); ++index)
        editors_.at(index).title->setText(plan.summaries.value(index));
    planValid_ = plan.valid && !editors_.isEmpty();
    const bool showError = !plan.valid && !editors_.isEmpty();
    error_->setText(showError ? plan.error : QString());
    error_->setVisible(showError);
    sqlPreview_->setPlainText(plan.statements.join(QStringLiteral(";\n\n")) +
                              (plan.statements.isEmpty() ? QString() : QStringLiteral(";")));
    updateActions();
}
void StructureEditorDialog::updateActions() {
    const bool loading = targetToken_ != 0 || indexesToken_ != 0 || keysToken_ != 0;
    const bool ready = adapter_ && !loading && !loadFailed_ && !isApplying();
    add_->setEnabled(ready);
    changeList_->setEnabled(!isApplying());
    if (tableName_)
        tableName_->setEnabled(!isApplying());
    apply_->setEnabled(ready && planValid_);
    cancel_->setEnabled(!isApplying());
}
void StructureEditorDialog::apply() {
    if (!adapter_ || isApplying())
        return;
    updateReview();
    if (!planValid_)
        return;
    applyToken_ = nextRequestToken();
    QString error;
    if (!adapter_->applyStructure(connection_, table(), changes(), applyToken_, &error)) {
        applyToken_ = 0;
        showStatus(error, true, false);
        updateActions();
        return;
    }
    showStatus(tr("Applying the structure changes…"), false, true);
    updateActions();
}
void StructureEditorDialog::handleEvent(const BridgeEvent& value) {
    if (value.id != connection_)
        return;
    const auto kind = fromRust(value.kind);
    if (kind == "disconnected" && (isApplying() || targetToken_ != 0)) {
        const bool applying = isApplying();
        applyToken_ = 0;
        targetToken_ = 0;
        loadFailed_ = true;
        showStatus(applying ? tr("The connection closed before the changes completed. Their "
                                 "outcome is unknown. %1")
                                  .arg(EngineAdapter::recoveryHint(
                                      RecoveryHint::ReconnectAndRefreshNavigator))
                            : tr("The connection closed."),
                   true, false);
        updateActions();
        return;
    }
    if (targetToken_ != 0 && value.request_token == targetToken_ &&
        (kind == "edit_target" || kind == "edit_target_failed")) {
        targetToken_ = 0;
        if (kind == "edit_target_failed") {
            failLoad(fromRust(value.error));
            return;
        }
        base_.qualifiedName = fromRust(value.edit_target.qualified_name);
        base_.columns.clear();
        for (const auto& column : value.edit_target.columns)
            base_.columns.push_back(
                {fromRust(column.name), fromRust(column.database_type), column.nullable});
        if (indexesToken_ == 0 && keysToken_ == 0 && !loadFailed_)
            statusLine_->hide();
        updateReview();
        return;
    }
    if (!isApplying() || value.request_token != applyToken_ ||
        (kind != "edit_applied" && kind != "edit_failed"))
        return;
    applyToken_ = 0;
    if (kind == "edit_failed") {
        showStatus(fromRust(value.error), true, false);
        apply_->setText(tr("Retry"));
        updateActions();
        return;
    }
    emit applied(connection_);
    const auto count = static_cast<int>(editors_.size());
    closeWithSuccess(base_.existing ? tr("Structure changed") : tr("Table created"),
                     tr("Applied %n change(s).", nullptr, count));
}
void StructureEditorDialog::handleInspection(quint64 connection, quint64 token,
                                             const ObjectInspection& inspection) {
    if (connection != connection_ || token == 0 || (token != indexesToken_ && token != keysToken_))
        return;
    if (inspection.availability == MetadataAvailability::Unavailable) {
        failLoad(inspection.reason);
        return;
    }
    if (token == indexesToken_) {
        indexesToken_ = 0;
        base_.indexes.clear();
        for (const auto& row : inspection.rows)
            base_.indexes.push_back(row.name);
    } else {
        keysToken_ = 0;
        base_.keys.clear();
        for (const auto& row : inspection.rows)
            base_.keys.push_back({row.name, row.kind});
    }
    if (targetToken_ == 0 && indexesToken_ == 0 && keysToken_ == 0 && !loadFailed_)
        statusLine_->hide();
    updateActions();
}
void StructureEditorDialog::failLoad(const QString& error) {
    loadFailed_ = true;
    targetToken_ = indexesToken_ = keysToken_ = 0;
    showStatus(error, true, false);
    updateActions();
}
void StructureEditorDialog::showStatus(const QString& message, bool error, bool busy) {
    statusLine_->setBusy(busy);
    statusLine_->setState(error ? design::StatusLine::State::Error
                                : design::StatusLine::State::Neutral);
    statusLine_->setMessage(message);
    statusLine_->setVisible(!message.isEmpty());
}
void StructureEditorDialog::reject() {
    if (!isApplying())
        DialogShell::reject();
}
}
