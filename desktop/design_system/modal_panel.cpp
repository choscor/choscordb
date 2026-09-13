#include "design_system/modal_panel.h"
#include "design_system/theme.h"
#include <QApplication>
#include <QEvent>
#include <QHideEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>

namespace choscordb::design {
namespace {
class Backdrop final : public QWidget {
  public:
    Backdrop(QWidget* parent, QDialog& dialog) : QWidget(parent), dialog_(dialog) {
        setObjectName("modalBackdrop");
        setFocusPolicy(Qt::NoFocus);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0, 26));
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            dialog_.reject();
            event->accept();
        }
    }

  private:
    QDialog& dialog_;
};
} // namespace
DialogPresentation::DialogPresentation(QDialog& dialog) : QObject(&dialog), dialog_(dialog) {}
DialogPresentation::~DialogPresentation() {
    delete backdrop_.data();
}
void DialogPresentation::center() {
    if (!owner_) {
        return;
    }
    if (backdrop_) {
        backdrop_->setGeometry(owner_->rect());
    }
    dialog_.move(owner_->mapToGlobal(owner_->rect().center()) - dialog_.rect().center());
}
void DialogPresentation::shown() {
    if (!dialog_.isModal()) {
        return;
    }
    auto* parent = dialog_.parentWidget();
    auto* owner = parent ? parent->window() : nullptr;
    if (owner_ != owner) {
        if (owner_) {
            owner_->removeEventFilter(this);
        }
        delete backdrop_.data();
        owner_ = owner;
        if (owner_) {
            owner_->installEventFilter(this);
        }
    }
    previousFocus_ = owner_ ? owner_->focusWidget() : QApplication::focusWidget();
    if (owner_) {
        if (!backdrop_) {
            backdrop_ = new Backdrop(owner_, dialog_);
        }
        backdrop_->show();
        backdrop_->raise();
        center();
    }
}
void DialogPresentation::hidden() {
    if (backdrop_) {
        backdrop_->hide();
    }
    if (previousFocus_ && previousFocus_->isVisible() && previousFocus_->isEnabled()) {
        previousFocus_->window()->activateWindow();
        previousFocus_->setFocus(Qt::OtherFocusReason);
    }
    previousFocus_.clear();
}
bool DialogPresentation::eventFilter(QObject* watched, QEvent* event) {
    if (watched == owner_ && dialog_.isVisible() && dialog_.isModal() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        if (event->type() == QEvent::Resize) {
            QEvent request(QEvent::LayoutRequest);
            QApplication::sendEvent(&dialog_, &request);
        }
        center();
    }
    return QObject::eventFilter(watched, event);
}
void paintDialogSurface(QWidget& widget) {
    QPainter painter(&widget);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto theme = resolvedThemeForWidget(widget);
    const auto& colors = theme.colors;
    painter.setBrush(colors.popover);
    auto border = colors.foreground;
    border.setAlphaF(theme.forcedContrast ? 1. : .1);
    painter.setPen(QPen(border, 1));
    const auto cornerRadius = resolveMetrics(Density::Compact, true).dialogRadius;
    painter.drawRoundedRect(QRectF(widget.rect()).adjusted(.5, .5, -.5, -.5), cornerRadius,
                            cornerRadius);
}
ModalPanel::ModalPanel(QWidget* parent) : QDialog(parent), presentation_(*this) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_WindowPropagation);
    if (parent) {
        setPalette(parent->palette());
    }
    setWindowModality(Qt::WindowModal);
    setProperty("appDialog", true);
}
ModalPanel::~ModalPanel() = default;
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
    QDialog::showEvent(event);
    presentation_.shown();
}
void ModalPanel::hideEvent(QHideEvent* event) {
    QDialog::hideEvent(event);
    presentation_.hidden();
}
} // namespace choscordb::design
