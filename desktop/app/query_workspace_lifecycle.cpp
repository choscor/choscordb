#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include <QMessageBox>
#include <QPushButton>

namespace choscordb {
bool QueryWorkspace::confirmShutdown() {
    const bool transaction = !pendingTransactions_.isEmpty();
    const bool active = externalWork_ || busy_ || fetching_ || viewBusy_ || exporting_ ||
                        (query_ && !executionFinished_);
    if (!transaction && !active)
        return true;
    ConfirmationDialog box(
        QMessageBox::Warning, tr("Close database sessions"),
        transaction
            ? tr("Uncommitted transactions will be rolled back and active work cancelled.")
            : tr("Active database work will be cancelled and any uncommitted changes rolled back."),
        QMessageBox::NoButton, widgets_.dialogParent);
    box.setTextFormat(Qt::PlainText);
    auto* close =
        box.addButton(transaction ? tr("Roll back and close") : tr("Cancel work and close"),
                      QMessageBox::DestructiveRole);
    auto* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.exec();
    return box.clickedButton() == close;
}

void QueryWorkspace::beginShutdown() {
    stopping_ = true;
    updateActions();
    adapter_->beginShutdown();
}

void QueryWorkspace::cancelShutdown() {
    stopping_ = false;
    updateActions();
}

void QueryWorkspace::shutdown() {
    stopping_ = true;
    adapter_->shutdown();
    updateActions();
}
} // namespace choscordb
