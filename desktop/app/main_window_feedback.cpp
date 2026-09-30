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

void MainWindow::elideResultSource(QLabel* source) {
    const auto full = source->property("fullSource").toString();
    const auto visible = source->fontMetrics().elidedText(full, Qt::ElideMiddle, source->width());
    if (source->text() != visible)
        source->setText(visible);
}

void MainWindow::elideResultOutcome(QLabel* outcome) {
    const auto full = outcome->property("fullOutcome").toString();
    if (full.isEmpty())
        return;
    const auto visible = outcome->fontMetrics().elidedText(full, Qt::ElideRight, outcome->width());
    if (outcome->text() != visible)
        outcome->setText(visible);
}

void MainWindow::fitResultFooter(QWidget* footer) {
    if (!footer)
        return;
    auto* source = footer->findChild<QLabel*>("executionSummary");
    auto* outcome = footer->findChild<QLabel*>("executionStateCompact");
    auto* previous = footer->findChild<QPushButton*>("previousPage");
    auto* next = footer->findChild<QPushButton*>("nextPage");
    if (!source || !outcome || !previous || !next)
        return;
    outcome->setMaximumWidth(qMax(outcome->minimumWidth(), footer->width() / 3));
    auto* layout = footer->layout();
    const auto margins = layout->contentsMargins();
    const int spacing = layout->spacing();
    int space =
        footer->width() - margins.left() - margins.right() - previous->sizeHint().width() -
        next->sizeHint().width() - qMin(outcome->sizeHint().width(), outcome->maximumWidth()) -
        source->fontMetrics().horizontalAdvance(QStringLiteral("Untitled query")) - 6 * spacing;
    for (const auto* name : {"statusLoadingIcon", "statusMessage"}) {
        if (auto* feedback = footer->findChild<QLabel*>(QString::fromLatin1(name));
            feedback && feedback->isVisible())
            space -= feedback->sizeHint().width() + spacing;
    }
    // Page context survives first; size, row count, then duration yield as space shrinks.
    for (const char* name :
         {"executionPage", "executionDuration", "executionRows", "executionVisibleSize"}) {
        auto* metric = footer->findChild<QLabel*>(QString::fromLatin1(name));
        if (!metric)
            continue;
        const bool show =
            !metric->text().isEmpty() && space >= metric->sizeHint().width() + spacing;
        metric->setVisible(show);
        if (show)
            space -= metric->sizeHint().width() + spacing;
    }
    elideResultSource(source);
    elideResultOutcome(outcome);
}
bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize &&
        watched->objectName() == QLatin1String("sqlResultFooter"))
        fitResultFooter(qobject_cast<QWidget*>(watched));
    else if (event->type() == QEvent::Resize &&
             watched->objectName() == QLatin1String("executionSummary"))
        elideResultSource(qobject_cast<QLabel*>(watched));
    else if (event->type() == QEvent::Resize &&
             watched->objectName() == QLatin1String("executionStateCompact"))
        elideResultOutcome(qobject_cast<QLabel*>(watched));
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
