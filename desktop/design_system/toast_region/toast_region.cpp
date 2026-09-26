#include "design_system/toast_region/toast_region.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/icons.h"
#include <QAccessible>
#include <QDialog>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
namespace choscordb {
ToastRegion::ToastRegion(QWidget* parent)
    : QLabel(parent), timer_(new QTimer(this)), opacity_(new QGraphicsOpacityEffect(this)),
      fade_(new QPropertyAnimation(opacity_, "opacity", this)) {
    setObjectName("toastRegion");
    setAccessibleName(tr("Notifications"));
    setTextFormat(Qt::PlainText);
    setWordWrap(true);
    // Reserve a separate column so wrapped notification text never meets the close button.
    setContentsMargins(0, 0, 36, 0);
    dismiss_ = new QToolButton(this);
    dismiss_->setObjectName("toastDismiss");
    dismiss_->setAccessibleName(tr("Dismiss notification"));
    dismiss_->setToolTip(tr("Dismiss notification"));
    dismiss_->setIconSize(QSize(14, 14));
    dismiss_->setFixedSize(24, 24);
    connect(dismiss_, &QToolButton::clicked, this, &ToastRegion::clearNotice);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &ToastRegion::clearNotice);
    setGraphicsEffect(opacity_);
    fade_->setDuration(180);
    connect(fade_, &QPropertyAnimation::finished, this, [this] {
        if (dismissing_) {
            clear();
            progress_->hide();
            hide();
            dismissing_ = false;
        }
    });
    progress_ = new QProgressBar(this);
    progress_->setObjectName("toastProgress");
    progress_->setRange(0, 0);
    progress_->setTextVisible(false);
    progress_->hide();
    hide();
}
void ToastRegion::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    dismiss_->move(qMax(0, width() - dismiss_->width() - 6), 6);
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
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    adjustSize();
    if (!progress_->isHidden()) {
        setFixedHeight(qMax(sizeHint().height() + 28, 72));
        progress_->setGeometry(12, height() - 20, this->width() - 24, 8);
    }
    move(qMax(0, overlayHost_->width() - this->width() - 16),
         objectName() == QLatin1String("progressToast")
             ? 16
             : qMax(0, overlayHost_->height() - this->height() - 16));
    raise();
}
void ToastRegion::showToast(const QString& title, const QString& body, ToastVariant variant,
                            int durationMs) {
    progress_->hide();
    const char* name = variant == ToastVariant::Success   ? "success"
                       : variant == ToastVariant::Warning ? "warning"
                                                          : "danger";
    setProperty("variant", name);
    style()->unpolish(this);
    style()->polish(this);
    setTextFormat(Qt::RichText);
    display(QStringLiteral("<b>%1</b><br/>%2").arg(title.toHtmlEscaped(), body.toHtmlEscaped()));
    setAccessibleDescription(title + QStringLiteral(". ") + body);
    if (durationMs > 0)
        timer_->start(durationMs);
}
void ToastRegion::showProgress(const QString& title, const QString& detail) {
    const bool updating = isVisible() && !dismissing_ && !progress_->isHidden() &&
                          property("variant").toString() == QLatin1String("progress");
    setProperty("variant", "progress");
    style()->unpolish(this);
    style()->polish(this);
    progress_->show();
    setTextFormat(Qt::RichText);
    const auto content =
        QStringLiteral("<b>%1</b>%2")
            .arg(title.toHtmlEscaped(),
                 detail.isEmpty() ? QString()
                                  : QStringLiteral("<br/>%1").arg(detail.toHtmlEscaped()));
    if (updating) {
        setText(content);
        placeOverlay();
    } else {
        display(content);
    }
    setAccessibleDescription(title +
                             (detail.isEmpty() ? QString() : QStringLiteral(". ") + detail));
}
void ToastRegion::display(const QString& text) {
    timer_->stop();
    fade_->stop();
    dismissing_ = false;
    dismiss_->setIcon(
        design::themedIcon(design::Icon::Close, palette().color(QPalette::WindowText), 14));
    setText(text);
    setVisible(!text.isEmpty());
    if (!text.isEmpty())
        placeOverlay();
    if (!text.isEmpty())
        opacity_->setOpacity(1.0);
    QAccessibleEvent announcement(this, QAccessible::Alert);
    QAccessible::updateAccessibility(&announcement);
}
void ToastRegion::clearNotice() {
    timer_->stop();
    fade_->stop();
    if (isHidden()) {
        progress_->hide();
        clear();
        dismissing_ = false;
        return;
    }
    dismissing_ = true;
    fade_->setStartValue(opacity_->opacity());
    fade_->setEndValue(0.0);
    fade_->start();
}
ToastRegion* windowToast(QWidget* context) {
    if (!context)
        return nullptr;
    auto* host = context->window();
    while (auto* dialog = qobject_cast<QDialog*>(host)) {
        if (!dialog->parentWidget())
            break;
        auto* owner = dialog->parentWidget()->window();
        if (owner == host)
            break;
        host = owner;
    }
    auto* toast =
        host->findChild<ToastRegion*>(QStringLiteral("toastRegion"), Qt::FindDirectChildrenOnly);
    if (!toast) {
        toast = new ToastRegion(host);
        toast->attachTo(host);
    }
    auto* modal = design::DialogPresentation::activeDialog(host);
    QObject* popupOwner = modal && (context == modal || modal->isAncestorOf(context))
                              ? modal
                              : nullptr;
    if (toast->property("embeddedPopupOwner").value<QObject*>() != popupOwner) {
        toast->setProperty("embeddedPopupOwner", QVariant::fromValue(popupOwner));
        if (popupOwner)
            QObject::connect(popupOwner, &QObject::destroyed, toast, [toast, popupOwner] {
                if (toast->property("embeddedPopupOwner").value<QObject*>() == popupOwner)
                    toast->setProperty("embeddedPopupOwner",
                                       QVariant::fromValue(static_cast<QObject*>(nullptr)));
            });
    }
    return toast;
}
ToastRegion* progressToast(QWidget* host) {
    if (!host)
        return nullptr;
    auto* toast =
        host->findChild<ToastRegion*>(QStringLiteral("progressToast"), Qt::FindDirectChildrenOnly);
    if (!toast) {
        toast = new ToastRegion(host);
        toast->setObjectName("progressToast");
        toast->attachTo(host);
    }
    return toast;
}
void clearProgressToast(QWidget* host) {
    if (host) {
        if (auto* toast = host->findChild<ToastRegion*>(QStringLiteral("progressToast"),
                                                        Qt::FindDirectChildrenOnly))
            toast->clearNotice();
    }
}
} // namespace choscordb
