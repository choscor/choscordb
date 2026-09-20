#pragma once
#include <QLabel>
#include <QPointer>
class QTimer;
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
    // A nonpositive duration keeps the toast visible until clearNotice().
    void showToast(const QString& title, const QString& body, ToastVariant variant,
                   int durationMs = 5000);
    void clearNotice();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void display(const QString& text);
    void placeOverlay();
    QPointer<QWidget> overlayHost_;
    QTimer* timer_;
    QGraphicsOpacityEffect* opacity_;
    QPropertyAnimation* fade_;
    bool dismissing_ = false;
};
} // namespace choscordb
