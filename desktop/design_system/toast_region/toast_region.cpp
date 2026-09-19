#include "design_system/toast_region/toast_region.h"
#include <QAccessible>
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
void ToastRegion::showNotice(const QString& text, int durationMs) {
    showPersistent(text);
    timer_->start(durationMs);
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
    timer_->start(durationMs);
}
void ToastRegion::showPersistent(const QString& text) {
    setProperty("variant", QString());
    style()->unpolish(this);
    style()->polish(this);
    setTextFormat(Qt::PlainText);
    display(text);
    setAccessibleDescription(text);
}
void ToastRegion::display(const QString& text) {
    timer_->stop();
    fade_->stop();
    dismissing_ = false;
    setText(text);
    setVisible(!text.isEmpty());
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
