#include "design_system/toast_region/toast_region.h"
#include "design_system/button/button.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include <QAccessible>
#include <QDialog>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
namespace choscordb {
namespace {
// Row 1 is the bold title. Row 2 is the body in the muted text color.
QString cardContent(const QString& title, const QString& body, const QColor& muted) {
    if (body.isEmpty())
        return QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped());
    return QStringLiteral("<b>%1</b><br/><span style=\"color:%2\">%3</span>")
        .arg(title.toHtmlEscaped(), muted.name(), body.toHtmlEscaped());
}
} // namespace
ToastRegion::ToastRegion(QWidget* parent)
    : QLabel(parent), timer_(new QTimer(this)), opacity_(new QGraphicsOpacityEffect(this)),
      fade_(new QPropertyAnimation(opacity_, "opacity", this)) {
    setObjectName("toastRegion");
    setAccessibleName(tr("Notifications"));
    setTextFormat(Qt::PlainText);
    setWordWrap(true);
    icon_ = new QLabel(this);
    icon_->setObjectName("toastIcon");
    icon_->setFixedSize(design::dimension(design::Dimension::Icon),
                        design::dimension(design::Dimension::Icon));
    icon_->hide();
    dismiss_ = design::createDismissButton(tr("Dismiss notification"), this);
    dismiss_->setObjectName("toastDismiss");
    dismiss_->setFocusPolicy(Qt::TabFocus);
    dismiss_->installEventFilter(this);
    dismiss_->setFixedSize(dismiss_->sizeHint());
    connect(dismiss_, &QPushButton::clicked, this, &ToastRegion::dismissNotice);
    // Reserve a leading status-icon column and a trailing column so wrapped
    // notification text never meets the icon or the close button.
    applyCardMargins(0);
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
    return design::spacing(design::Spacing::Three) + design::dimension(design::Dimension::Icon) +
           design::spacing(design::Spacing::Two);
}
int ToastRegion::titleRowHeight() const {
    return std::max(
        {dismiss_->height(), design::dimension(design::Dimension::Icon), fontMetrics().height()});
}
void ToastRegion::applyCardMargins(int bottomExtra) {
    const int inset = design::spacing(design::Spacing::Three);
    // Center the first text line on the title row, as the icon and the X are.
    const int top = inset + (titleRowHeight() - fontMetrics().height()) / 2;
    setContentsMargins(leadingInset(), top,
                       inset + dismiss_->width() + design::spacing(design::Spacing::Two),
                       inset + bottomExtra);
}
void ToastRegion::showStatusIcon(design::Icon icon, const QColor& color) {
    const int size = design::dimension(design::Dimension::Icon);
    icon_->setPixmap(design::themedIcon(icon, color, size).pixmap(QSize(size, size)));
    icon_->show();
}
void ToastRegion::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    const int inset = design::spacing(design::Spacing::Three);
    const int row = titleRowHeight();
    dismiss_->move(qMax(0, width() - dismiss_->width() - inset),
                   inset + (row - dismiss_->height()) / 2);
    icon_->move(inset, inset + (row - icon_->height()) / 2);
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
    const int width = qMin(design::dimension(design::Dimension::ToastWidth),
                           qMax(1, overlayHost_->width() - 2 * inset));
    const int heightLimit = qMax(1, overlayHost_->height() - 2 * inset);
    setFixedWidth(width);
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    details_->hide();
    applyCardMargins(0);
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
        applyCardMargins(details_->height() + design::spacing(design::Spacing::Two));
        auto title = fullTitle_;
        auto body = fullBody_;
        const auto muted = design::resolvedThemeForWidget(*this).colors.fgMuted;
        const auto content = [&muted](const QString& title, const QString& body) {
            return cardContent(title, body, muted);
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
    auto* sections = dialog->createSections();
    auto* heading = new design::Text(dialog->windowTitle(), sections);
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss = sections->addDismissButton(tr("Close notification details"));
    dismiss->setObjectName("toastDetailsDismiss");
    connect(dismiss, &QPushButton::clicked, dialog, &QDialog::reject);
    auto* text = new QPlainTextEdit(sections);
    text->setObjectName("toastDetailsText");
    text->setAccessibleName(tr("Full notification details"));
    text->setReadOnly(true);
    text->setPlainText(fullTitle_ + QStringLiteral("\n\n") + fullBody_);
    sections->bodyLayout()->addWidget(text);
    auto* close = new design::Button(tr("Close"), sections);
    close->setObjectName("toastDetailsClose");
    close->setVariant(design::ButtonVariant::Outline);
    sections->footerLayout()->addStretch();
    sections->footerLayout()->addWidget(close);
    connect(close, &QPushButton::clicked, dialog, &QDialog::reject);
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
    // The replacing notice waits in its own slot while other notices show.
    const bool replacingShown = currentNotice_ && currentNotice_->count > 0;
    if (pinned_ || (currentNotice_ && !dismissing_ && !replacingShown)) {
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
    display(cardContent(title, body, colors.fgMuted));
    startReadingTimer(durationMs);
}
std::optional<ToastRegion::Notice> ToastRegion::replacingNotice() const {
    if (replacingNotices_.empty())
        return std::nullopt;
    auto notice = replacingNotices_.back();
    notice.count = int(replacingNotices_.size());
    return notice;
}
void ToastRegion::refreshReplacingNotice() {
    const bool showing = currentNotice_ && currentNotice_->count > 0 && !dismissing_;
    if (showing && replacingNotices_.empty()) {
        clearNotice();
        return;
    }
    if (showing || (!pinned_ && (!currentNotice_ || dismissing_) && queuedNotices_.empty() &&
                    !replacingNotices_.empty())) {
        currentNotice_ = replacingNotice();
        renderNotice(*currentNotice_);
    }
}
void ToastRegion::showReplacingToast(const QString& title, const QString& body,
                                     ToastVariant variant) {
    std::erase_if(replacingNotices_,
                  [&body](const Notice& notice) { return notice.detail == body; });
    replacingNotices_.push_back(Notice{title, body, variant, 0, false, 1});
    refreshReplacingNotice();
}
void ToastRegion::clearReplacingToast(const QString& body) {
    if (std::erase_if(replacingNotices_,
                      [&body](const Notice& notice) { return notice.detail == body; }) == 0)
        return;
    refreshReplacingNotice();
}
void ToastRegion::renderNotice(const Notice& notice) {
    if (notice.progress) {
        renderProgress(notice.title, notice.detail);
        return;
    }
    const auto title = notice.count > 1
                           ? tr("%1 · %n notices", nullptr, notice.count).arg(notice.title)
                           : notice.title;
    renderToast(title, notice.detail, notice.variant, notice.durationMs);
}
void ToastRegion::showPinnedToast(const QString& title, const QString& body, ToastVariant variant) {
    // The replacing notice keeps its own slot and comes back after the queue.
    if (!pinned_ && currentNotice_ && !dismissing_ && currentNotice_->count == 0)
        queuedNotices_.push_front(*currentNotice_);
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
    // A shown replacing notice that ends was dismissed with all the messages it holds.
    if (currentNotice_ && currentNotice_->count > 0)
        replacingNotices_.clear();
    currentNotice_.reset();
    if (!queuedNotices_.empty()) {
        currentNotice_ = queuedNotices_.front();
        queuedNotices_.pop_front();
    } else if (!replacingNotices_.empty()) {
        currentNotice_ = replacingNotice();
    } else {
        clearVisibleNotice();
        return;
    }
    renderNotice(*currentNotice_);
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
        cardContent(title, detail, design::resolvedThemeForWidget(*this).colors.fgMuted);
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
