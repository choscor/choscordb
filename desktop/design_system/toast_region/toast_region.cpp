#include "design_system/toast_region/toast_region.h"
#include <QAccessible>
#include <QTimer>
namespace choscordb {
ToastRegion::ToastRegion(QWidget* parent) : QLabel(parent), timer_(new QTimer(this)) {
    setObjectName("toastRegion");
    setAccessibleName(tr("Notifications"));
    setTextFormat(Qt::PlainText);
    setWordWrap(true);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &ToastRegion::clearNotice);
    hide();
}
void ToastRegion::showNotice(const QString& text, int durationMs) {
    showPersistent(text);
    timer_->start(durationMs);
}
void ToastRegion::showPersistent(const QString& text) {
    timer_->stop();
    setText(text);
    setVisible(!text.isEmpty());
    QAccessibleEvent announcement(this, QAccessible::Alert);
    QAccessible::updateAccessibility(&announcement);
}
void ToastRegion::clearNotice() {
    timer_->stop();
    clear();
    hide();
}
} // namespace choscordb
