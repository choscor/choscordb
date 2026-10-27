#include "app/error_feedback.h"
#include "app/main_window.h"
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace choscordb {
void MainWindow::reportError(const QString& message) {
    errorFeedback_->reportError(message);
}
void MainWindow::reportNotice(const QString& message, ToastVariant variant) {
    errorFeedback_->reportNotice(message, variant);
}
void MainWindow::clearNotice(const QString& message) {
    errorFeedback_->clearNotice(message);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == savedConnectionsList_.data() && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Space || key->key() == Qt::Key_Select ||
             key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            key->modifiers() == Qt::NoModifier && activateFocusedSavedProfile_) {
            activateFocusedSavedProfile_();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

} // namespace choscordb
