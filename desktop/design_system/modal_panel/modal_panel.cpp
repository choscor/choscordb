#include "design_system/modal_panel/modal_panel.h"
#include "design_system/theme.h"

#include <QEvent>
#include <QHideEvent>
#include <QLayout>
#include <QShowEvent>

namespace choscordb::design {
ModalPanel::ModalPanel(QWidget* parent) : QDialog(parent), presentation_(*this) {
    // Own the frameless surface and shadow. Application modality uses a regular
    // Cocoa modal session instead of a native sheet with a second corner mask.
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_WindowPropagation);
    if (parent) {
        setPalette(parent->palette());
    }
    setWindowModality(Qt::ApplicationModal);
    setProperty("appDialog", true);
}
ModalPanel::~ModalPanel() = default;
void ModalPanel::open() {
    // QDialog::open() forces window modality (a native sheet on Cocoa).
    // Retain asynchronous opening while preserving the app-owned panel mode.
    setWindowModality(Qt::ApplicationModal);
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
        const int padding = spacing(Spacing::Four);
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
