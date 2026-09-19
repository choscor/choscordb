#pragma once
#include <QLabel>
class QTimer;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
namespace choscordb {
enum class ToastVariant { Success, Warning, Danger };
class ToastRegion final : public QLabel {
    Q_OBJECT
  public:
    explicit ToastRegion(QWidget* parent = nullptr);
    void showNotice(const QString& text, int durationMs = 5000);
    void showToast(const QString& title, const QString& body, ToastVariant variant,
                   int durationMs = 5000);
    void showPersistent(const QString& text);
    void clearNotice();

  private:
    void display(const QString& text);
    QTimer* timer_;
    QGraphicsOpacityEffect* opacity_;
    QPropertyAnimation* fade_;
    bool dismissing_ = false;
};
} // namespace choscordb
