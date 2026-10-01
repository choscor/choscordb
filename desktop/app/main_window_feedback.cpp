#include "app/main_window.h"
#include "design_system/status_line/status_line.h"
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace choscordb {
void MainWindow::addFeedbackStatusLines(QWidget* host) {
    auto* layout = qobject_cast<QVBoxLayout*>(host->layout());
    for (const auto* scope : {"workspace", "appearance", "application", "preferences", "completion",
                              "connection", "lifecycle"}) {
        auto* line = new design::StatusLine(host);
        line->setObjectName(QString::fromLatin1(scope) + QStringLiteral("StatusLine"));
        line->hide();
        layout->addWidget(line);
    }
}

void MainWindow::showStatus(const QString& message, ToastVariant variant, const QString& scope,
                            const QString& owner) {
    auto* line = findChild<design::StatusLine*>(scope + QStringLiteral("StatusLine"));
    if (!line)
        return;
    line->setProperty("noticeOwner", owner);
    line->setBusy(false);
    line->setAvailable(variant != ToastVariant::Danger);
    if (variant != ToastVariant::Danger)
        line->setNeutral();
    line->setProperty("variant", variant == ToastVariant::Danger    ? "danger"
                                 : variant == ToastVariant::Warning ? "warning"
                                                                    : "success");
    line->setMessage(message);
    line->setVisible(!message.isEmpty());
}
void MainWindow::showStatusProgress(const QString& message, const QString& scope) {
    showStatus(message, ToastVariant::Success, scope);
    if (auto* line = findChild<design::StatusLine*>(scope + QStringLiteral("StatusLine")))
        line->setBusy(true);
}
void MainWindow::clearStatus(const QString& scope, const QString& expectedMessage,
                             const QString& owner) {
    auto* line = findChild<design::StatusLine*>(scope + QStringLiteral("StatusLine"));
    if (line && (expectedMessage.isEmpty() || line->accessibleDescription() == expectedMessage) &&
        (owner.isEmpty() || line->property("noticeOwner").toString() == owner)) {
        line->setBusy(false);
        line->setMessage({});
        line->hide();
    }
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
