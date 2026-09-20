#include "design_system/toast_region/toast_region.h"
#include <QAccessible>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QStyle>
#include <QTimer>
namespace choscordb {
ToastRegion::ToastRegion(QWidget* parent)
    : QLabel(parent), timer_(new QTimer(this)), opacity_(new QGraphicsOpacityEffect(this)),
      fade_(new QPropertyAnimation(opacity_, "opacity", this)) {
    setObjectName("toastRegion");
    setAccessibleName(tr("Notifications"));
    setTextFormat(Qt::PlainText);
    setWordWrap(true);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &ToastRegion::clearNotice);
    setGraphicsEffect(opacity_);
    fade_->setDuration(180);
    connect(fade_, &QPropertyAnimation::finished, this, [this] {
        if (dismissing_) {
            clear();
            hide();
            dismissing_ = false;
        }
    });
    hide();
}
void ToastRegion::attachTo(QWidget* host) {
    if (!host)
        return;
    if (overlayHost_)
        overlayHost_->removeEventFilter(this);
    const bool visible = isVisible();
    setParent(host);
    overlayHost_ = host;
    host->installEventFilter(this);
    placeOverlay();
    if (visible)
        show();
}
bool ToastRegion::eventFilter(QObject* watched, QEvent* event) {
    if (watched == overlayHost_ && event->type() == QEvent::Resize)
        placeOverlay();
    return QLabel::eventFilter(watched, event);
}
void ToastRegion::placeOverlay() {
    if (!overlayHost_)
        return;
    const int width = qMin(320, qMax(1, overlayHost_->width() - 32));
    setFixedWidth(width);
    adjustSize();
    move(qMax(0, overlayHost_->width() - this->width() - 16),
         qMax(0, overlayHost_->height() - this->height() - 16));
    raise();
}
void ToastRegion::showToast(const QString& title, const QString& body, ToastVariant variant,
                            int durationMs) {
    const char* name = variant == ToastVariant::Success ? "success"
                       : variant == ToastVariant::Warning ? "warning"
                                                           : "danger";
    setProperty("variant", name);
    style()->unpolish(this);
    style()->polish(this);
    setTextFormat(Qt::RichText);
    display(QStringLiteral("<b>%1</b><br/>%2")
                .arg(title.toHtmlEscaped(), body.toHtmlEscaped()));
    setAccessibleDescription(title + QStringLiteral(". ") + body);
    if (durationMs > 0)
        timer_->start(durationMs);
}
void ToastRegion::display(const QString& text) {
    timer_->stop();
    fade_->stop();
    dismissing_ = false;
    setText(text);
    setVisible(!text.isEmpty());
    if (!text.isEmpty())
        placeOverlay();
    if (!text.isEmpty()) {
        opacity_->setOpacity(0.0);
        fade_->setStartValue(0.0);
        fade_->setEndValue(1.0);
        fade_->start();
    }
    QAccessibleEvent announcement(this, QAccessible::Alert);
    QAccessible::updateAccessibility(&announcement);
}
void ToastRegion::clearNotice() {
    timer_->stop();
    fade_->stop();
    if (isHidden()) {
        clear();
        dismissing_ = false;
        return;
    }
    dismissing_ = true;
    fade_->setStartValue(opacity_->opacity());
    fade_->setEndValue(0.0);
    fade_->start();
}
} // namespace choscordb
