#include "design_system/modal_panel/modal_panel.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/theme.h"

#include <QHideEvent>
#include <QLayout>
#include <QShowEvent>
#include <QVBoxLayout>

namespace choscordb::design {
ModalDialog::ModalDialog(QWidget* parent) : QDialog(parent), presentation_(*this) {
    presentation_.makeModal();
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_WindowPropagation);
    if (parent) {
        setPalette(parent->palette());
    }
    setProperty("appDialog", true);
}
ModalDialog::~ModalDialog() = default;
DialogSections* ModalDialog::createSections() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* sections = new DialogSections(this);
    root->addWidget(sections);
    return sections;
}
void ModalDialog::open() {
    // Keep the asynchronous QDialog contract without requesting a native sheet.
    setResult(0);
    show();
}
QSize ModalDialog::sizeHint() const {
    const_cast<ModalDialog*>(this)->ensurePolished();
    const int width = parentWidget() ? qMin(dimension(Dimension::ModalWidth),
                                            qMax(1, parentWidget()->width() - 32))
                                     : dimension(Dimension::ModalWidth);
    auto hint = QDialog::sizeHint();
    hint.setWidth(width);
    if (layout() && layout()->hasHeightForWidth()) {
        hint.setHeight(layout()->heightForWidth(width));
    }
    return hint;
}
void ModalDialog::paintEvent(QPaintEvent*) {
    paintDialogSurface(*this);
}
void ModalDialog::showEvent(QShowEvent* event) {
    resize(sizeHint());
    QDialog::showEvent(event);
    presentation_.shown();
}
void ModalDialog::hideEvent(QHideEvent* event) {
    QDialog::hideEvent(event);
    presentation_.hidden();
}
} // namespace choscordb::design
