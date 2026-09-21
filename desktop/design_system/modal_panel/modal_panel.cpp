#include "design_system/modal_panel/modal_panel.h"
#include "design_system/theme.h"

#include <QEvent>
#include <QHideEvent>
#include <QLayout>
#include <QShowEvent>

namespace choscordb::design {
ModalPanel::ModalPanel(QWidget* parent) : QDialog(parent), presentation_(*this) {
    presentation_.makeModal();
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_WindowPropagation);
    if (parent) {
        setPalette(parent->palette());
    }
    setProperty("appDialog", true);
}
ModalPanel::~ModalPanel() = default;
void ModalPanel::setEdgeToEdgeContent(bool enabled) {
    edgeToEdgeContent_ = enabled;
    if (layout()) {
        const int padding = enabled ? 0 : spacing(Spacing::Four);
        layout()->setContentsMargins(padding, padding, padding, padding);
        layout()->setSpacing(padding);
    }
}
void ModalPanel::open() {
    // Keep the asynchronous QDialog contract without requesting a native sheet.
    setResult(0);
    show();
}
QSize ModalPanel::sizeHint() const {
    const_cast<ModalPanel*>(this)->ensurePolished();
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
bool ModalPanel::event(QEvent* event) {
    if (event->type() == QEvent::Polish && layout()) {
        const int padding = edgeToEdgeContent_ ? 0 : spacing(Spacing::Four);
        layout()->setContentsMargins(padding, padding, padding, padding);
        layout()->setSpacing(padding);
    }
    return QDialog::event(event);
}
void ModalPanel::paintEvent(QPaintEvent*) {
    paintDialogSurface(*this);
}
void ModalPanel::showEvent(QShowEvent* event) {
    resize(sizeHint());
    QDialog::showEvent(event);
    presentation_.shown();
}
void ModalPanel::hideEvent(QHideEvent* event) {
    QDialog::hideEvent(event);
    presentation_.hidden();
}
} // namespace choscordb::design
