#include "app/error_feedback.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include <QApplication>
#include <QEvent>
namespace choscordb {
ErrorFeedback::ErrorFeedback(QWidget* window, ToastRegion* toast)
    : QObject(window), window_(window), toast_(toast) {}
void ErrorFeedback::reportError(const QString& message) {
    errors_.append(message);
    present();
}
void ErrorFeedback::reportNotice(const QString& message, ToastVariant variant) {
    if (toast_)
        toast_->showReplacingToast(variant == ToastVariant::Danger ? tr("Error") : tr("Warning"),
                                   message, variant);
}
void ErrorFeedback::clearNotice(const QString& message) {
    if (toast_)
        toast_->clearReplacingToast(message);
}
void ErrorFeedback::present() {
    if (errors_.isEmpty())
        return;
    const auto count =
        errors_.size() == 1 ? tr("1 error:") : tr("%n errors:", nullptr, int(errors_.size()));
    const auto text = count + QStringLiteral("\n\n• ") + errors_.join(QStringLiteral("\n• "));
    if (dialog_) {
        dialog_->setText(text);
        return;
    }
    if (blocker_)
        return;
    QWidget* blocker = design::DialogPresentation::activeDialog(window_);
    if (!blocker)
        blocker = QApplication::activeModalWidget();
    if (blocker) {
        // Two modals never stack. The list shows when the open modal hides or goes away.
        blocker_ = blocker;
        blocker->installEventFilter(this);
        connect(blocker, &QObject::destroyed, this, &ErrorFeedback::retryAfterBlocker,
                Qt::QueuedConnection);
        return;
    }
    auto* dialog = new ConfirmationDialog(QMessageBox::Critical, tr("Something went wrong"), text,
                                          QMessageBox::Close, window_);
    dialog->setObjectName("errorFeedbackDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, [this] {
        dialog_.clear();
        errors_.clear();
    });
    dialog_ = dialog;
    dialog->open();
}
bool ErrorFeedback::eventFilter(QObject* watched, QEvent* event) {
    if (watched == blocker_ && event->type() == QEvent::Hide)
        QMetaObject::invokeMethod(this, &ErrorFeedback::retryAfterBlocker, Qt::QueuedConnection);
    return QObject::eventFilter(watched, event);
}
void ErrorFeedback::retryAfterBlocker() {
    if (blocker_) {
        blocker_->removeEventFilter(this);
        disconnect(blocker_, nullptr, this, nullptr);
    }
    blocker_.clear();
    present();
}
} // namespace choscordb
