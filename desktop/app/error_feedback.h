#pragma once
#include "design_system/toast_region/toast_region.h"
#include <QObject>
#include <QPointer>
#include <QStringList>
namespace choscordb {
class ConfirmationDialog;
class ErrorFeedback final : public QObject {
    Q_OBJECT
  public:
    ErrorFeedback(QWidget* window, ToastRegion* toast);
    void reportError(const QString& message);
    void reportNotice(const QString& message, ToastVariant variant = ToastVariant::Warning);
    void clearNotice(const QString& message);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void present();
    void retryAfterBlocker();
    QWidget* window_;
    QPointer<ToastRegion> toast_;
    QStringList errors_;
    QPointer<ConfirmationDialog> dialog_;
    QPointer<QWidget> blocker_;
};
}
