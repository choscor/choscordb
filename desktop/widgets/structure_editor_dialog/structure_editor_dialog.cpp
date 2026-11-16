#include "widgets/structure_editor_dialog/structure_editor_dialog.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "widgets/structure_editor/structure_editor.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace choscordb {
StructureEditorDialog::StructureEditorDialog(EngineAdapter* adapter, quint64 connection,
                                             StructureTable table, const QString& targetName,
                                             QWidget* parent)
    : DialogShell(parent) {
    setObjectName("structureEditorDialog");
    const auto title = tr("Design new table");
    setWindowTitle(title);
    setAppModal();
    resize(design::dialogInitialSize(design::DialogSize::Profiles));

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
    auto* scope = createDescription(tr("Design a new table in %1.").arg(targetName), body);
    scope->setObjectName("structureEditorScope");
    bodyLayout->addWidget(scope);
    auto* form = sections->addForm();
    auto* tableName = new QLineEdit(this);
    tableName->setObjectName("structureEditorTableName");
    sections->addFormRow(form, tr("Table name"), tableName);

    table.existing = false;
    editor_ = new StructureEditor(adapter, connection, std::move(table), {}, body);
    editor_->setObjectName("structureEditor");
    bodyLayout->addWidget(editor_, 1);
    connect(tableName, &QLineEdit::textChanged, editor_, &StructureEditor::setTableName);

    statusLine_ = sections->addStatusLine();
    statusLine_->setObjectName("structureEditorStatusLine");
    cancel_ = sections->addAction(design::DialogActionRole::Dismiss, tr("Cancel"));
    cancel_->setObjectName("structureEditorCancel");
    apply_ = sections->addAction(design::DialogActionRole::Primary, tr("Apply"));
    apply_->setObjectName("structureEditorApply");
    connect(cancel_, &QPushButton::clicked, this, &StructureEditorDialog::reject);
    connect(apply_, &QPushButton::clicked, editor_,
            [this] { editor_->apply(editor_->plannedSql()); });
    connect(editor_, &StructureEditor::stateChanged, this, &StructureEditorDialog::updateActions);
    connect(editor_, &StructureEditor::statusChanged, this, &StructureEditorDialog::showStatus);
    connect(editor_, &StructureEditor::applyFailed, this, [this] { apply_->setText(tr("Retry")); });
    connect(editor_, &StructureEditor::applied, this, [this](quint64 changed) {
        emit applied(changed);
        closeWithSuccess(tr("Table created"),
                         tr("Applied %n change(s).", nullptr, int(editor_->changeCount())));
    });
    updateActions();
}
void StructureEditorDialog::updateActions() {
    if (!apply_)
        return;
    apply_->setEnabled(editor_->canApply());
    cancel_->setEnabled(!editor_->isApplying());
}
void StructureEditorDialog::showStatus(const QString& message, bool error, bool busy) {
    const auto progress = error ? QString() : message;
    statusLine_->setBusy(busy);
    statusLine_->setMessage(progress);
    statusLine_->setVisible(!progress.isEmpty());
}
void StructureEditorDialog::reject() {
    if (!editor_->isApplying())
        DialogShell::reject();
}
}
