#pragma once
#include "design_system/icons.h"

#include <QLabel>
#include <QPointer>
#include <deque>
#include <optional>
#include <vector>
class QTimer;
class QDialog;
class QToolButton;
namespace choscordb::design {
class Button;
}
class QProgressBar;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
namespace choscordb {
enum class ToastVariant { Success, Warning, Danger };
class ToastRegion final : public QLabel {
    Q_OBJECT
  public:
    explicit ToastRegion(QWidget* parent = nullptr);
    // Render above host content without taking space in its layout.
    void attachTo(QWidget* host);
    // Completion notices reserve at least ten seconds of reading time. Warnings
    // and failures require dismissal. Later notices wait in display order.
    void showToast(const QString& title, const QString& body, ToastVariant variant,
                   int durationMs = 10000);
    // An important notice stays in front of ordinary notifications until resolved or dismissed.
    void showPinnedToast(const QString& title, const QString& body, ToastVariant variant);
    // One notice for background failures and warnings. It shows the latest active message
    // and the count of active messages; it never queues a second copy. It stays until it is
    // dismissed, and it waits while other notices show.
    void showReplacingToast(const QString& title, const QString& body, ToastVariant variant);
    // Removes the message `body`; the notice hides when no active message is left.
    void clearReplacingToast(const QString& body);
    void clearPinnedToast();
    // Compatibility for the gallery; production progress belongs in StatusLine.
    void showProgress(const QString& title, const QString& detail = {});
    void clearNotice();

  protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    [[nodiscard]] static int leadingInset();
    // The title row holds the status icon, the first text line and the X.
    [[nodiscard]] int titleRowHeight() const;
    void applyCardMargins(int bottomExtra);
    void showStatusIcon(design::Icon icon, const QColor& color);
    struct Notice {
        QString title;
        QString detail;
        ToastVariant variant = ToastVariant::Success;
        int durationMs = 0;
        bool progress = false;
        // The number of notices that a replacing notice holds; 0 for other notices.
        int count = 0;
    };
    void renderNotice(const Notice& notice);
    [[nodiscard]] std::optional<Notice> replacingNotice() const;
    void refreshReplacingNotice();
    void display(const QString& text);
    void renderToast(const QString& title, const QString& body, ToastVariant variant,
                     int durationMs);
    void renderProgress(const QString& title, const QString& detail);
    void clearVisibleNotice();
    void showNextNotice();
    void updateReadingPause();
    void startReadingTimer(int durationMs);
    void dismissNotice();
    void placeOverlay();
    void openDetails();
    QPointer<QWidget> overlayHost_;
    QTimer* timer_;
    QLabel* icon_;
    design::Button* dismiss_;
    QToolButton* details_;
    QPointer<QDialog> detailsDialog_;
    QString fullTitle_;
    QString fullBody_;
    QString fullContent_;
    QProgressBar* progress_;
    QGraphicsOpacityEffect* opacity_;
    QPropertyAnimation* fade_;
    bool dismissing_ = false;
    bool pinned_ = false;
    std::optional<Notice> currentNotice_;
    std::deque<Notice> queuedNotices_;
    // The active messages of the replacing notice, oldest first.
    std::vector<Notice> replacingNotices_;
    int readingTimeLeft_ = 0;
    bool hovered_ = false;
};
// Shared window-level notification surface, including while an embedded modal is open.
ToastRegion* windowToast(QWidget* context);
// Feature widgets use one progress overlay per host and dismiss it when work ends.
ToastRegion* progressToast(QWidget* host);
void clearProgressToast(QWidget* host);
} // namespace choscordb
