#include "widgets/dialog_shell.h"

#include "design_system/theme_manager.h"

#include <QLabel>
#include <QLayout>
#include <QShowEvent>

namespace choscordb {

DialogShell::DialogShell(QWidget* parent) : QDialog(parent) {
    setProperty("appDialog", true);
    setModal(false);
}

QLabel* DialogShell::createDescription(const QString& text, QWidget* parent) {
    auto* description = new QLabel(text, parent);
    description->setProperty("dialogDescription", true);
    description->setTextFormat(Qt::PlainText);
    description->setWordWrap(true);
    return description;
}

QLabel* DialogShell::createInlineStatus(QWidget* parent) {
    auto* status = new QLabel(parent);
    status->setProperty("dialogStatus", true);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    return status;
}

void DialogShell::showEvent(QShowEvent* event) {
    applyLayoutMetrics();
    QDialog::showEvent(event);
}

void DialogShell::applyLayoutMetrics() {
    design::ThemeManager* theme = nullptr;
    for (auto* scope = static_cast<QObject*>(this); scope != nullptr; scope = scope->parent()) {
        theme = scope->findChild<design::ThemeManager*>(QString{}, Qt::FindDirectChildrenOnly);
        if (theme != nullptr) {
            break;
        }
    }
    if (theme == nullptr) {
        return;
    }
    if (!observingTheme_) {
        observingTheme_ = true;
        connect(theme, &design::ThemeManager::metricsChanged, this,
                [this] { applyLayoutMetrics(); });
    }
    const auto metrics = theme->metrics();
    if (auto* root = layout()) {
        root->setContentsMargins(metrics.dialogContentSpacing, metrics.dialogContentSpacing,
                                 metrics.dialogContentSpacing, metrics.dialogContentSpacing);
    }
    for (auto* childLayout : findChildren<QLayout*>()) {
        childLayout->setSpacing(metrics.spacingMedium);
    }
}

} // namespace choscordb
