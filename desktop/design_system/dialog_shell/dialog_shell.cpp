#include "design_system/dialog_shell/dialog_shell.h"

#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QHideEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTextEdit>
#include <QVBoxLayout>

namespace choscordb {

DialogShell::DialogShell(QWidget* parent) : QDialog(parent) {
    setProperty("appDialog", true);
    setModal(false);
    setAttribute(Qt::WA_WindowPropagation);
    setFont(design::resolveTypography(design::TextStyle::Body));
    presentation_ = new design::DialogPresentation(*this);
}

void DialogShell::setAppModal() {
    presentation_->makeModal();
}

void DialogShell::open() {
    if (property("embeddedModal").toBool() || windowModality() == Qt::ApplicationModal) {
        setResult(0);
        show();
    } else {
        QDialog::open();
    }
}

design::DialogSections* DialogShell::createSections() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* sections = new design::DialogSections(this);
    root->addWidget(sections);
    return sections;
}

QLabel* DialogShell::createDescription(const QString& text, QWidget* parent) {
    auto* description = new QLabel(text, parent);
    description->setProperty("dialogDescription", true);
    description->setProperty("designRole", "description");
    description->setTextFormat(Qt::PlainText);
    description->setWordWrap(true);
    return description;
}

QLabel* DialogShell::createInlineStatus(QWidget* parent) {
    auto* status = new QLabel(parent);
    status->setProperty("dialogStatus", true);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    return status;
}

namespace {
QHash<const QWidget*, QString> draftValues(const QWidget& root) {
    QHash<const QWidget*, QString> values;
    for (auto* widget : root.findChildren<QWidget*>()) {
        if (auto* line = qobject_cast<QLineEdit*>(widget)) {
            auto* owner = line->parentWidget();
            if (!line->isReadOnly() && !qobject_cast<QAbstractSpinBox*>(owner) &&
                !qobject_cast<QComboBox*>(owner) && !qobject_cast<QKeySequenceEdit*>(owner))
                values.insert(widget, line->text());
        } else if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget)) {
            values.insert(widget, spin->text());
        } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            values.insert(widget, combo->currentText());
        } else if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
            if (button->isCheckable())
                values.insert(widget, QString::number(button->isChecked()));
        } else if (auto* plain = qobject_cast<QPlainTextEdit*>(widget)) {
            if (!plain->isReadOnly())
                values.insert(widget, plain->toPlainText());
        } else if (auto* rich = qobject_cast<QTextEdit*>(widget)) {
            if (!rich->isReadOnly())
                values.insert(widget, rich->toPlainText());
        } else if (auto* keys = qobject_cast<QKeySequenceEdit*>(widget)) {
            values.insert(widget, keys->keySequence().toString(QKeySequence::PortableText));
        }
    }
    return values;
}
}

void DialogShell::markDraftClean() {
    cleanDraft_ = draftValues(*this);
}

bool DialogShell::hasDraft() const {
    return cleanDraft_ && *cleanDraft_ != draftValues(*this);
}

bool DialogShell::confirmDiscardDraft() {
    return !hasDraft() ||
           ConfirmationDialog::confirm(
               this, tr("Discard changes?"), tr("The changes in this dialog are not saved."),
               tr("Discard changes"), ConfirmationDialog::Consequence::Destructive);
}

void DialogShell::reject() {
    if (isVisible() && !confirmDiscardDraft())
        return;
    QDialog::reject();
}

void DialogShell::closeWithSuccess(const QString& title, const QString& detail) {
    if (auto* toast = windowToast(parentWidget()))
        toast->showToast(title, detail, ToastVariant::Success);
    accept();
}

void DialogShell::showEvent(QShowEvent* event) {
    ensureContentHeight();
    QDialog::showEvent(event);
    presentation_->shown();
}
void DialogShell::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (event->oldSize().width() != event->size().width())
        ensureContentHeight();
}
void DialogShell::hideEvent(QHideEvent* event) {
    QDialog::hideEvent(event);
    presentation_->hidden();
}
void DialogShell::paintEvent(QPaintEvent*) {
    design::paintDialogSurface(*this, property("embeddedModal").toBool() || isModal());
}

void DialogShell::ensureContentHeight() {
    if (auto* root = layout(); root && root->hasHeightForWidth()) {
        setMinimumHeight(root->totalMinimumHeightForWidth(width()));
    }
}

}
