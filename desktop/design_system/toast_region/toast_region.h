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
    void attachTo(QWidget* host);
    void showToast(const QString& title, const QString& body, ToastVariant variant,
                   int durationMs = 10000);
    void showPinnedToast(const QString& title, const QString& body, ToastVariant variant);
    void showReplacingToast(const QString& title, const QString& body, ToastVariant variant);
    void clearReplacingToast(const QString& body);
    void clearPinnedToast();
    void showProgress(const QString& title, const QString& detail = {});
    void clearNotice();

  protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    [[nodiscard]] static int leadingInset();
    [[nodiscard]] int titleRowHeight() const;
    void applyCardMargins(int bottomExtra);
    void showStatusIcon(design::Icon icon, const QColor& color);
    struct Notice {
        QString title;
        QString detail;
        ToastVariant variant = ToastVariant::Success;
        int durationMs = 0;
        bool progress = false;
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
    std::vector<Notice> replacingNotices_;
    int readingTimeLeft_ = 0;
    bool hovered_ = false;
};
ToastRegion* windowToast(QWidget* context);
ToastRegion* progressToast(QWidget* host);
void clearProgressToast(QWidget* host);
}
