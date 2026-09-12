#pragma once
#include <QLabel>
class QTimer;
namespace choscordb {
class ToastRegion final : public QLabel {
    Q_OBJECT
  public:
    explicit ToastRegion(QWidget* parent = nullptr);
    void showNotice(const QString& text, int durationMs = 5000);
    void showPersistent(const QString& text);
    void clearNotice();

  private:
    QTimer* timer_;
};
} // namespace choscordb
