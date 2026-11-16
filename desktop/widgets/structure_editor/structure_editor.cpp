#include "widgets/structure_editor/structure_editor.h"
#include "bridge/request_token.h"
#include "bridge/rust_text.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
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
#include <utility>

namespace choscordb {
namespace {
using bridge_detail::fromRust;
QLabel* description(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty("designRole", "description");
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}
}
StructureEditor::StructureEditor(EngineAdapter* adapter, quint64 connection, StructureTable table,
                                 const QString& objectId, QWidget* parent)
    : QWidget(parent), adapter_(adapter), connection_(connection), base_(std::move(table)),
      objectId_(objectId),
      kinds_(EngineAdapter::structureChangeKinds(base_.driver, !base_.existing)),
      types_(EngineAdapter::structureTypes(base_.driver)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(design::spacing(design::Spacing::Two));
    auto* top = new QHBoxLayout();
    top->setSpacing(design::spacing(design::Spacing::Four));
    top->addWidget(description(tr("Add changes, then review the SQL before you apply it."), this),
                   1);
    add_ = new design::Button(tr("Add change"), this);
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
    top->addWidget(add_, 0, Qt::AlignTop);
    layout->addLayout(top);

    changeList_ = new QWidget(this);
    changeList_->setObjectName("structureEditorChanges");
    changeList_->setAutoFillBackground(false);
    changeLayout_ = new QVBoxLayout(changeList_);
    changeLayout_->setContentsMargins(0, 0, 0, 0);
    changeLayout_->setSpacing(design::spacing(design::Spacing::Three));
    empty_ = description(tr("Add a change to see the SQL."), changeList_);
    empty_->setObjectName("structureEditorEmpty");
    changeLayout_->addWidget(empty_);
    changeLayout_->addStretch(1);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("structureEditorScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setWidget(changeList_);
    auto* split = new QHBoxLayout();
    split->setSpacing(design::spacing(design::Spacing::Four));
    split->addWidget(scroll, 3);
    auto* sqlColumn = new QVBoxLayout();
    sqlColumn->setSpacing(design::spacing(design::Spacing::Two));
    split->addLayout(sqlColumn, 2);
    layout->addLayout(split, 1);

    error_ = new QLabel(this);
    error_->setObjectName("structureEditorError");
    error_->setProperty("designRole", "fieldError");
    error_->setWordWrap(true);
    error_->hide();
    layout->addWidget(error_);
    sqlColumn->addWidget(description(tr("SQL to execute"), this));
    sqlPreview_ = new QPlainTextEdit(this);
    sqlPreview_->setObjectName("structureEditorSql");
    sqlPreview_->setAccessibleName(tr("SQL preview"));
    sqlPreview_->setReadOnly(true);
    sqlPreview_->setProperty("designRole", "codePreview");
    sqlPreview_->setFont(
        design::resolveTypography(design::TextStyle::Body, design::FontDesign::Monospaced));
    sqlPreview_->setFrameShape(QFrame::NoFrame);
    sqlColumn->addWidget(sqlPreview_, 1);

    reviewTimer_.setSingleShot(true);
    reviewTimer_.setInterval(EngineAdapter::filterDelayMs());
    connect(&reviewTimer_, &QTimer::timeout, this, &StructureEditor::updateReview);
    connect(adapter, &EngineAdapter::eventReady, this, &StructureEditor::handleEvent);
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
    updateReview();
}
void StructureEditor::load() {
    load(true);
}
void StructureEditor::load(bool announce) {
    if (!base_.existing || !adapter_ || disconnected_)
        return;
    loadFailed_ = false;
    loadAnnounced_ = announce;
    targetToken_ = nextRequestToken();
    indexesToken_ = nextRequestToken();
    keysToken_ = nextRequestToken();
    if (announce)
        emit statusChanged(tr("Loading the table structure…"), false, true);
    if (!adapter_->inspectEditTarget(connection_, objectId_, targetToken_)) {
        failLoad(tr("The table columns could not be loaded."));
        return;
    }
    adapter_->loadObjectInspection(connection_, objectId_, ObjectInspectionPane::Indexes,
                                   indexesToken_);
    adapter_->loadObjectInspection(connection_, objectId_, ObjectInspectionPane::Keys, keysToken_);
    updateActions();
}
void StructureEditor::setTableName(const QString& name) {
    tableName_ = name;
    reviewTimer_.start();
}
void StructureEditor::setPane(std::optional<ObjectInspectionPane> pane) {
    const auto actions = add_->menu()->actions();
    for (qsizetype index = 0; index < kinds_.size() && index < actions.size(); ++index)
        actions.at(index)->setVisible(!pane || kinds_.at(index).pane == *pane);
}
bool StructureEditor::canApply() const {
    return adapter_ && !loading() && !loadFailed_ && !isApplying() && planValid_;
}
QString StructureEditor::shownSql() const {
    return sqlPreview_->toPlainText();
}
QString StructureEditor::plannedSql() {
    updateReview();
    return shownSql();
}
StructureTable StructureEditor::table() const {
    auto table = base_;
    if (!base_.existing)
        table.tableName = tableName_;
    return table;
}
QList<StructureChange> StructureEditor::changes() const {
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
QWidget* StructureEditor::fieldWidget(const StructureField& field, QWidget* parent) {
    if (field.input == QStringLiteral("flag")) {
        auto* check = new QCheckBox(field.label, parent);
        connect(check, &QCheckBox::toggled, this, &StructureEditor::editChanges);
        return check;
    }
    if (field.input == QStringLiteral("text")) {
        auto* line = new QLineEdit(parent);
        connect(line, &QLineEdit::textChanged, this, [this] {
            clearApplyFailure();
            reviewTimer_.start();
        });
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
        connect(box, &QComboBox::editTextChanged, this, [this] {
            clearApplyFailure();
            reviewTimer_.start();
        });
    }
    connect(box, &QComboBox::currentIndexChanged, this, &StructureEditor::editChanges);
    return box;
}
void StructureEditor::addChange(const StructureChangeKindInfo& kind) {
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
    editChanges();
}
void StructureEditor::removeChange(QWidget* frame) {
    if (isApplying())
        return;
    for (qsizetype index = 0; index < editors_.size(); ++index) {
        if (editors_.at(index).frame != frame)
            continue;
        editors_.removeAt(index);
        frame->deleteLater();
        break;
    }
    editChanges();
}
void StructureEditor::clearApplyFailure() {
    if (!loadFailed_)
        failure_.clear();
}
void StructureEditor::editChanges() {
    clearApplyFailure();
    updateReview();
}
void StructureEditor::updateReview() {
    reviewTimer_.stop();
    empty_->setVisible(editors_.isEmpty());
    const auto plan = EngineAdapter::structurePlan(table(), changes());
    for (qsizetype index = 0; index < editors_.size(); ++index)
        editors_.at(index).title->setText(plan.summaries.value(index));
    planValid_ = plan.valid && !editors_.isEmpty();
    const auto error = !failure_.isEmpty()                  ? failure_
                       : !plan.valid && !editors_.isEmpty() ? plan.error
                                                            : QString();
    error_->setText(error);
    error_->setVisible(!error.isEmpty());
    sqlPreview_->setPlainText(plan.statements.join(QStringLiteral(";\n\n")) +
                              (plan.statements.isEmpty() ? QString() : QStringLiteral(";")));
    updateActions();
}
void StructureEditor::updateActions() {
    add_->setEnabled(adapter_ && !loading() && !loadFailed_ && !isApplying());
    changeList_->setEnabled(!isApplying());
    emit stateChanged();
}
bool StructureEditor::apply(const QString& reviewedSql) {
    if (!adapter_ || isApplying())
        return false;
    if (plannedSql() != reviewedSql) {
        fail(tr("The SQL changed after you reviewed it. Review the SQL again."));
        return false;
    }
    if (!canApply())
        return false;
    failure_.clear();
    updateReview();
    applyToken_ = nextRequestToken();
    QString error;
    if (!adapter_->applyStructure(connection_, table(), changes(), applyToken_, &error)) {
        applyToken_ = 0;
        fail(error);
        return false;
    }
    emit statusChanged(tr("Applying the structure changes…"), false, true);
    updateActions();
    return true;
}
void StructureEditor::handleEvent(const BridgeEvent& value) {
    if (value.id != connection_)
        return;
    const auto kind = fromRust(value.kind);
    if (kind == "disconnected") {
        if (disconnected_)
            return;
        disconnected_ = true;
        failLoad(
            isApplying()
                ? tr("The connection closed before the changes completed. Their outcome is "
                     "unknown. %1")
                      .arg(EngineAdapter::recoveryHint(RecoveryHint::ReconnectAndRefreshNavigator))
                : tr("The connection closed. Discard the staged changes, reconnect and edit "
                     "the structure again."));
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
        finishLoadStep();
        updateReview();
        return;
    }
    if (!isApplying() || value.request_token != applyToken_ ||
        (kind != "edit_applied" && kind != "edit_failed"))
        return;
    applyToken_ = 0;
    if (kind == "edit_failed") {
        fail(fromRust(value.error));
        emit applyFailed(connection_);
        load(false);
        return;
    }
    updateActions();
    emit applied(connection_);
}
void StructureEditor::handleInspection(quint64 connection, quint64 token,
                                       const ObjectInspection& inspection) {
    if (connection != connection_ || token == 0)
        return;
    const bool indexes = token == indexesToken_;
    const bool keys = token == keysToken_;
    if (!indexes && !keys)
        return;
    if (inspection.availability == MetadataAvailability::Unavailable) {
        failLoad(inspection.reason);
        return;
    }
    if (indexes) {
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
    finishLoadStep();
    updateActions();
}
void StructureEditor::finishLoadStep() {
    if (!loading() && !loadFailed_ && std::exchange(loadAnnounced_, false))
        emit statusChanged({}, false, false);
}
void StructureEditor::failLoad(const QString& error) {
    loadFailed_ = true;
    applyToken_ = targetToken_ = indexesToken_ = keysToken_ = 0;
    fail(error);
}
void StructureEditor::fail(const QString& error) {
    failure_ = error;
    updateReview();
    emit statusChanged(error, true, false);
}
}
