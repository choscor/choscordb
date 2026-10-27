#pragma once
#include "design_system/toast_region/toast_region.h"
#include <QObject>
#include <QPointer>
#include <QStringList>
namespace choscordb {
class ConfirmationDialog;
// The window's error feedback. An error from an action that the user started joins one
// modal list. A background error or a blocked-action warning replaces one toast.
class ErrorFeedback final : public QObject {
    Q_OBJECT
  public:
    ErrorFeedback(QWidget* window, ToastRegion* toast);
    void reportError(const QString& message);
    void reportNotice(const QString& message, ToastVariant variant = ToastVariant::Warning);
    // Removes the notice when the condition that `message` tells about is resolved.
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
    // The open modal that the error list waits for.
    QPointer<QWidget> blocker_;
};
} // namespace choscordb
