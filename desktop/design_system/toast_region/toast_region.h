#pragma once
#include <QLabel>
#include <QPointer>
#include <deque>
#include <optional>
class QTimer;
class QDialog;
class QToolButton;
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
    void clearPinnedToast();
    // Compatibility for the gallery; production progress belongs in StatusLine.
    void showProgress(const QString& title, const QString& detail = {});
    void clearNotice();

  protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    struct Notice {
        QString title;
        QString detail;
        ToastVariant variant = ToastVariant::Success;
        int durationMs = 0;
        bool progress = false;
    };
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
    QToolButton* dismiss_;
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
    int readingTimeLeft_ = 0;
    bool hovered_ = false;
};
// Shared window-level notification surface, including while an embedded modal is open.
ToastRegion* windowToast(QWidget* context);
// Feature widgets use one progress overlay per host and dismiss it when work ends.
ToastRegion* progressToast(QWidget* host);
void clearProgressToast(QWidget* host);
} // namespace choscordb
