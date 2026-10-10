#include "design_system/toast_region/toast_region.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "design_system/theme.h"
#include <QAccessible>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
namespace choscordb {
ToastRegion::ToastRegion(QWidget* parent)
    : QLabel(parent), timer_(new QTimer(this)), opacity_(new QGraphicsOpacityEffect(this)),
      fade_(new QPropertyAnimation(opacity_, "opacity", this)) {
    setObjectName("toastRegion");
    setAccessibleName(tr("Notifications"));
    setTextFormat(Qt::PlainText);
    setWordWrap(true);
    // Reserve a leading status-icon column and a trailing column so wrapped
    // notification text never meets the icon or the close button.
    setContentsMargins(leadingInset(), 0, 36, 0);
    icon_ = new QLabel(this);
    icon_->setObjectName("toastIcon");
    icon_->setFixedSize(design::dimension(design::Dimension::Icon),
                        design::dimension(design::Dimension::Icon));
    icon_->hide();
    dismiss_ = new QToolButton(this);
    dismiss_->setObjectName("toastDismiss");
    dismiss_->setAccessibleName(tr("Dismiss notification"));
    dismiss_->setToolTip(tr("Dismiss notification"));
    dismiss_->installEventFilter(this);
    dismiss_->setIconSize(QSize(14, 14));
    dismiss_->setFixedSize(24, 24);
    connect(dismiss_, &QToolButton::clicked, this, &ToastRegion::dismissNotice);
    details_ = new QToolButton(this);
    details_->setObjectName("toastDetails");
    details_->setText(tr("Details…"));
    details_->setAccessibleName(tr("Read full notification details"));
    details_->installEventFilter(this);
    details_->hide();
    connect(details_, &QToolButton::clicked, this, &ToastRegion::openDetails);
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
int ToastRegion::leadingInset() {
    return design::dimension(design::Dimension::Icon) + design::spacing(design::Spacing::Two);
}
void ToastRegion::showStatusIcon(design::Icon icon, const QColor& color) {
    const int size = design::dimension(design::Dimension::Icon);
    icon_->setPixmap(design::themedIcon(icon, color, size).pixmap(QSize(size, size)));
    icon_->show();
}
void ToastRegion::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    dismiss_->move(qMax(0, width() - dismiss_->width() - 6), 6);
    icon_->move(design::spacing(design::Spacing::Two), design::spacing(design::Spacing::Two));
    details_->move(
        design::spacing(design::Spacing::Three),
        qMax(0, height() - details_->height() - design::spacing(design::Spacing::Three)));
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
bool ToastRegion::event(QEvent* event) {
    if (event->type() == QEvent::Enter)
        hovered_ = true;
    else if (event->type() == QEvent::Leave)
        hovered_ = false;
    const bool result = QLabel::event(event);
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave ||
        event->type() == QEvent::Show || event->type() == QEvent::Hide)
        updateReadingPause();
    return result;
}
void ToastRegion::startReadingTimer(int durationMs) {
    timer_->stop();
    readingTimeLeft_ = qMax(0, durationMs);
    timer_->setInterval(readingTimeLeft_);
    updateReadingPause();
}
void ToastRegion::updateReadingPause() {
    const bool paused = hovered_ || dismiss_->hasFocus() || details_->hasFocus() ||
                        (detailsDialog_ && detailsDialog_->isVisible()) || !isVisible();
    if (paused && timer_->isActive()) {
        readingTimeLeft_ = qMax(1, timer_->remainingTime());
        timer_->stop();
    } else if (!paused && readingTimeLeft_ > 0 && !timer_->isActive()) {
        timer_->start(readingTimeLeft_);
    }
}
bool ToastRegion::eventFilter(QObject* watched, QEvent* event) {
    if ((watched == dismiss_ || watched == details_) &&
        (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut))
        QTimer::singleShot(0, this, &ToastRegion::updateReadingPause);
    if (watched == overlayHost_ && event->type() == QEvent::Resize)
        placeOverlay();
    return QLabel::eventFilter(watched, event);
}
void ToastRegion::placeOverlay() {
    if (!overlayHost_)
        return;
    const auto inset = design::spacing(design::Spacing::Four);
    const int width = qMin(320, qMax(1, overlayHost_->width() - 2 * inset));
    const int heightLimit = qMax(1, overlayHost_->height() - 2 * inset);
    setFixedWidth(width);
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    details_->hide();
    setContentsMargins(leadingInset(), 0, 36, 0);
    setText(fullContent_);
    const bool progressVisible = !progress_->isHidden();
    const int progressSpace = progressVisible ? design::spacing(design::Spacing::Eight) : 0;
    const auto measuredHeight = [this, progressSpace, progressVisible] {
        const auto textHeight =
            qMax(sizeHint().height(), heightForWidth(this->width())) + progressSpace;
        return progressVisible
                   ? qMax(textHeight, design::dimension(design::Dimension::ToastProgress))
                   : textHeight;
    };
    if (measuredHeight() > heightLimit) {
        details_->adjustSize();
        details_->show();
        setContentsMargins(leadingInset(), 0, 36,
                           details_->height() + design::spacing(design::Spacing::Six));
        auto title = fullTitle_;
        auto body = fullBody_;
        const auto content = [](const QString& title, const QString& body) {
            return QStringLiteral("<b>%1</b><br/>%2")
                .arg(title.toHtmlEscaped(), body.toHtmlEscaped());
        };
        setText(content(title, {}));
        if (measuredHeight() > heightLimit) {
            title = tr("Notification");
            body = fullTitle_ + QStringLiteral("\n") + fullBody_;
        }
        // Measure the real styled label, retaining only a whole UTF-16 prefix.
        const auto prefix = [&body](qsizetype length) {
            if (length > 0 && length < body.size() && body.at(length - 1).isHighSurrogate())
                --length;
            return body.left(length) + QStringLiteral("…");
        };
        qsizetype low = 0;
        qsizetype high = body.size();
        while (low < high) {
            const auto middle = low + (high - low + 1) / 2;
            setText(content(title, prefix(middle)));
            if (measuredHeight() <= heightLimit)
                low = middle;
            else
                high = middle - 1;
        }
        setText(content(title, prefix(low)));
    }
    setFixedHeight(qMin(heightLimit, measuredHeight()));
    if (progressVisible) {
        const auto spacing = design::spacing(design::Spacing::Three);
        const auto progressHeight = design::spacing(design::Spacing::Two);
        const int detailsSpace = details_->isHidden() ? 0 : details_->height() + spacing;
        progress_->setGeometry(spacing, qMax(0, height() - spacing - progressHeight - detailsSpace),
                               qMax(1, this->width() - 2 * spacing), progressHeight);
    }
    details_->move(
        design::spacing(design::Spacing::Three),
        qMax(0, height() - details_->height() - design::spacing(design::Spacing::Three)));
    move(qMax(0, overlayHost_->width() - this->width() - inset),
         qMax(0, overlayHost_->height() - this->height() - inset));
    raise();
}
void ToastRegion::openDetails() {
    if (detailsDialog_) {
        detailsDialog_->raise();
        detailsDialog_->activateWindow();
        return;
    }
    auto* dialog = new DialogShell(overlayHost_ ? overlayHost_.data() : window());
    dialog->setObjectName("toastDetailsDialog");
    dialog->setWindowTitle(tr("Notification details"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(design::dialogInitialSize(design::DialogSize::Detail));
    auto* layout = new QVBoxLayout(dialog);
    auto* text = new QPlainTextEdit(dialog);
    text->setObjectName("toastDetailsText");
    text->setAccessibleName(tr("Full notification details"));
    text->setReadOnly(true);
    text->setPlainText(fullTitle_ + QStringLiteral("\n\n") + fullBody_);
    layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    detailsDialog_ = dialog;
    connect(dialog, &QDialog::finished, this, [this] {
        detailsDialog_.clear();
        updateReadingPause();
    });
    dialog->open();
    updateReadingPause();
}
void ToastRegion::showToast(const QString& title, const QString& body, ToastVariant variant,
                            int durationMs) {
    // Warnings and failures require deliberate dismissal. Timed completion notices
    // reserve reading time even when a caller requests a shorter duration.
    if (durationMs > 0) {
        const auto readingMs = qMin<qsizetype>(std::numeric_limits<int>::max(),
                                               2000 + (title.size() + body.size()) * 60);
        durationMs = variant == ToastVariant::Success
                         ? qMax(qMax(10000, durationMs), static_cast<int>(readingMs))
                         : 0;
    }
    const Notice notice{title, body, variant, durationMs, false};
    const auto same = [&notice](const Notice& other) {
        return !other.progress && other.title == notice.title && other.detail == notice.detail &&
               other.variant == notice.variant;
    };
    if ((currentNotice_ && same(*currentNotice_)) ||
        std::any_of(queuedNotices_.begin(), queuedNotices_.end(), same))
        return;
    if (pinned_ || (currentNotice_ && !dismissing_)) {
        queuedNotices_.push_back(notice);
        return;
    }
    currentNotice_ = notice;
    renderToast(title, body, variant, durationMs);
}
void ToastRegion::renderToast(const QString& title, const QString& body, ToastVariant variant,
                              int durationMs) {
    fullTitle_ = title;
    fullBody_ = body;
    progress_->hide();
    const char* name = variant == ToastVariant::Success   ? "success"
                       : variant == ToastVariant::Warning ? "warning"
                                                          : "danger";
    setProperty("variant", name);
    style()->unpolish(this);
    style()->polish(this);
    const auto& colors = design::resolvedThemeForWidget(*this).colors;
    showStatusIcon(variant == ToastVariant::Success   ? design::Icon::Check
                   : variant == ToastVariant::Warning ? design::Icon::Warning
                                                      : design::Icon::Error,
                   variant == ToastVariant::Success   ? colors.success
                   : variant == ToastVariant::Warning ? colors.warning
                                                      : colors.danger);
    setTextFormat(Qt::RichText);
    setAccessibleDescription(title + QStringLiteral(". ") + body);
    display(QStringLiteral("<b>%1</b><br/>%2").arg(title.toHtmlEscaped(), body.toHtmlEscaped()));
    startReadingTimer(durationMs);
}
void ToastRegion::showPinnedToast(const QString& title, const QString& body, ToastVariant variant) {
    if (!pinned_ && currentNotice_ && !dismissing_) {
        queuedNotices_.push_front(*currentNotice_);
    }
    currentNotice_.reset();
    pinned_ = true;
    renderToast(title, body, variant, 0);
}
void ToastRegion::clearPinnedToast() {
    if (!pinned_)
        return;
    pinned_ = false;
    showNextNotice();
}
void ToastRegion::showProgress(const QString& title, const QString& detail) {
    const Notice notice{title, detail, ToastVariant::Success, 0, true};
    if (pinned_ || (currentNotice_ && !currentNotice_->progress && !dismissing_)) {
        for (auto& pending : queuedNotices_) {
            if (pending.progress && pending.title == title) {
                pending = notice;
                return;
            }
        }
        queuedNotices_.push_back(notice);
        return;
    }
    currentNotice_ = notice;
    renderProgress(title, detail);
}
void ToastRegion::showNextNotice() {
    currentNotice_.reset();
    if (queuedNotices_.empty()) {
        clearVisibleNotice();
        return;
    }
    currentNotice_ = queuedNotices_.front();
    queuedNotices_.pop_front();
    const auto notice = *currentNotice_;
    if (notice.progress)
        renderProgress(notice.title, notice.detail);
    else
        renderToast(notice.title, notice.detail, notice.variant, notice.durationMs);
}
void ToastRegion::renderProgress(const QString& title, const QString& detail) {
    fullTitle_ = title;
    fullBody_ = detail;
    const bool updating = isVisible() && !dismissing_ && !progress_->isHidden() &&
                          property("variant").toString() == QLatin1String("progress");
    setProperty("variant", "progress");
    style()->unpolish(this);
    style()->polish(this);
    showStatusIcon(design::Icon::Loader, design::resolvedThemeForWidget(*this).colors.fgMuted);
    progress_->show();
    setTextFormat(Qt::RichText);
    const auto content =
        QStringLiteral("<b>%1</b>%2")
            .arg(title.toHtmlEscaped(),
                 detail.isEmpty() ? QString()
                                  : QStringLiteral("<br/>%1").arg(detail.toHtmlEscaped()));
    setAccessibleDescription(title +
                             (detail.isEmpty() ? QString() : QStringLiteral(". ") + detail));
    if (updating) {
        fullContent_ = content;
        setText(content);
        placeOverlay();
    } else {
        display(content);
    }
    setAccessibleDescription(title +
                             (detail.isEmpty() ? QString() : QStringLiteral(". ") + detail));
}
void ToastRegion::display(const QString& text) {
    fullContent_ = text;
    readingTimeLeft_ = 0;
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
    QAccessibleEvent announcement(this, property("variant") == "danger"
                                            ? QAccessible::Alert
                                            : QAccessible::DescriptionChanged);
    QAccessible::updateAccessibility(&announcement);
}
void ToastRegion::clearNotice() {
    if (pinned_)
        return;
    showNextNotice();
}
void ToastRegion::dismissNotice() {
    if (pinned_)
        clearPinnedToast();
    else
        clearNotice();
}
void ToastRegion::clearVisibleNotice() {
    readingTimeLeft_ = 0;
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
    QObject* popupOwner =
        modal && (context == modal || modal->isAncestorOf(context)) ? modal : nullptr;
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
