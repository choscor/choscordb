#include "design_system/dialog_shell/dialog_shell.h"

#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/theme_manager.h"

#include <QHideEvent>
#include <QLabel>
#include <QLayout>
#include <QResizeEvent>
#include <QShowEvent>
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
    design::paintDialogSurface(*this, false);
}

void DialogShell::ensureContentHeight() {
    if (auto* root = layout(); root && root->hasHeightForWidth()) {
        // Scrollable content may prefer more room without requiring it. Only grow
        // for the layout's minimum, including labels that wrap at this width.
        setMinimumHeight(root->totalMinimumHeightForWidth(width()));
    }
}

} // namespace choscordb
