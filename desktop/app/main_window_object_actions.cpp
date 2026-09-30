#include "app/main_window.h"

#include "app/navigator_controller.h"
#include "app/object_action_sql.h"
#include "app/object_data_workspace.h"
#include "app/object_explorer.h"
#include "app/object_tab_title.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "models/navigator_model.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace choscordb {
namespace {
QString bridgeText(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
} // namespace

bool MainWindow::objectActionReady(quint64 connection, const QString& objectId, const QString& kind,
                                   const QString& parentObjectId, const QString& qualifiedName,
                                   const QString& relationSubtype) {
    if (pendingObjectAction_ || pendingObjectRefresh_) {
        showStatus(tr("Finish the current object action and navigator refresh before trying "
                      "again."),
                   ToastVariant::Warning, QStringLiteral("navigator"));
        return false;
    }
    auto* selector = findChild<QComboBox*>("connectionSelector");
    if (databaseClosePending_ || !workspace_ || !navigatorController_ || !selector ||
        selector->findData(QVariant::fromValue<qulonglong>(connection)) < 0 ||
        workspace_->driverForConnection(connection).isEmpty()) {
        showStatus(tr("The selected connection is no longer available."), ToastVariant::Danger,
                   QStringLiteral("navigator"));
        return false;
    }
    if (!navigatorController_->model()->matchesObject(connection, objectId, kind, qualifiedName,
                                                      parentObjectId, relationSubtype)) {
        showStatus(tr("The selected object changed. Refresh the navigator and try again."),
                   ToastVariant::Warning, QStringLiteral("navigator"));
        return false;
    }
    if (!workspace_->navigationAllowed() || workspace_->hasPendingEdits() ||
        workspace_->activeManualTransaction(connection)) {
        showStatus(tr("Finish or cancel active database work and pending edits before changing "
                      "this object."),
                   ToastVariant::Warning, QStringLiteral("navigator"));
        return false;
    }
    for (int i = 0; i < editors_->count(); ++i) {
        auto* object = qobject_cast<ObjectExplorer*>(editors_->widget(i));
        if (!object || object->property("objectConnection").toULongLong() != connection)
            continue;
        if (object->operationInFlight()) {
            showStatus(tr("Finish the active object inspection before changing this object."),
                       ToastVariant::Warning, QStringLiteral("navigator"));
            return false;
        }
        if (auto* objectData = object->findChild<ObjectDataWorkspace*>())
            for (auto* result : objectData->findChildren<QueryWorkspace*>())
                if (!result->navigationAllowed() || result->hasPendingEdits()) {
                    showStatus(tr("Finish or cancel active object data work and pending edits "
                                  "before changing this object."),
                               ToastVariant::Warning, QStringLiteral("navigator"));
                    return false;
                }
    }
    return true;
}

void MainWindow::requestObjectAction(const QString& action, quint64 connection,
                                     const QString& objectId, const QString& shortName,
                                     const QString& kind, const QString& parentObjectId,
                                     const QString& qualifiedName, const QString& relationSubtype) {
    if (!objectActionReady(connection, objectId, kind, parentObjectId, qualifiedName,
                           relationSubtype))
        return;
    const auto driver = workspace_->driverForConnection(connection);
    const auto displayKind =
        relationSubtype == QStringLiteral("materialized_view") ? tr("materialized view")
        : relationSubtype == QStringLiteral("foreign_table")   ? tr("foreign table")
                                                               : kind;
    ObjectActionStatement statement;
    QString newShortName;
    if (action == QStringLiteral("drop")) {
        statement = ObjectActionSql::drop(driver, kind, objectId, qualifiedName, relationSubtype);
        if (!statement.valid) {
            showStatus(statement.error, ToastVariant::Danger, QStringLiteral("navigator"));
            return;
        }
        ConfirmationDialog dialog(
            QMessageBox::Warning, tr("Drop %1").arg(displayKind),
            tr("Drop %1 %2?\n\nSQL to execute:\n%3").arg(displayKind, qualifiedName, statement.sql),
            QMessageBox::NoButton, this);
        dialog.setObjectName("dropObjectDialog");
        dialog.setTextFormat(Qt::PlainText);
        auto* confirm =
            dialog.addButton(tr("Drop %1").arg(displayKind), QMessageBox::DestructiveRole);
        confirm->setObjectName("dropObjectConfirm");
        auto* cancel = dialog.addButton(QMessageBox::Cancel);
        dialog.setDefaultButton(cancel);
        dialog.exec();
        if (dialog.clickedButton() != confirm)
            return;
    } else if (action == QStringLiteral("rename")) {
        DialogShell dialog(this);
        dialog.setObjectName("renameObjectDialog");
        dialog.setWindowTitle(tr("Rename %1").arg(displayKind));
        auto* layout = new QVBoxLayout(&dialog);
        layout->addWidget(dialog.createDescription(
            tr("Rename %1 %2 in the same schema or database.").arg(displayKind, qualifiedName),
            &dialog));
        auto* name = new QLineEdit(&dialog);
        name->setObjectName("renameObjectName");
        name->setAccessibleName(tr("New unqualified name"));
        name->setText(shortName);
        layout->addWidget(name);
        layout->addWidget(dialog.createDescription(tr("SQL to execute"), &dialog));
        auto* preview = new QPlainTextEdit(&dialog);
        preview->setObjectName("renameObjectSql");
        preview->setAccessibleName(tr("SQL preview"));
        preview->setReadOnly(true);
        layout->addWidget(preview);
        auto* status = dialog.createInlineStatus(&dialog);
        status->setObjectName("renameObjectValidation");
        layout->addWidget(status);
        auto* buttons = new QDialogButtonBox(&dialog);
        auto* confirm = new design::Button(tr("Rename"), &dialog);
        confirm->setObjectName("renameObjectConfirm");
        auto* cancel = new design::Button(tr("Cancel"), &dialog);
        cancel->setObjectName("renameObjectCancel");
        cancel->setVariant(design::ButtonVariant::Outline);
        buttons->addButton(confirm, QDialogButtonBox::AcceptRole);
        buttons->addButton(cancel, QDialogButtonBox::RejectRole);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
        const auto update = [&] {
            const auto next = ObjectActionSql::rename(driver, kind, objectId, qualifiedName,
                                                      name->text(), relationSubtype);
            preview->setPlainText(next.sql);
            status->setText(next.valid ? QString{} : next.error);
            confirm->setEnabled(next.valid);
        };
        connect(name, &QLineEdit::textChanged, &dialog, update);
        update();
        name->setFocus();
        name->selectAll();
        if (dialog.exec() != QDialog::Accepted)
            return;
        statement = ObjectActionSql::rename(driver, kind, objectId, qualifiedName, name->text(),
                                            relationSubtype);
        newShortName = name->text();
    } else {
        return;
    }
    if (!statement.valid || !objectActionReady(connection, objectId, kind, parentObjectId,
                                               qualifiedName, relationSubtype)) {
        if (!statement.valid)
            showStatus(statement.error, ToastVariant::Danger, QStringLiteral("navigator"));
        return;
    }
    const auto profileId = workspace_->profileIdForConnection(connection);
    const auto context = profileId.isEmpty() ? QStringLiteral("session:%1").arg(connection)
                                             : QStringLiteral("profile:%1").arg(profileId);
    const auto query = workspace_->adapter()->execute(connection, statement.sql);
    if (!query) {
        showStatus(tr("The object action could not be submitted. Check the connection and retry."),
                   ToastVariant::Danger, QStringLiteral("navigator"));
        return;
    }
    pendingObjectAction_ = PendingObjectAction{action,
                                               objectId,
                                               kind,
                                               displayKind,
                                               parentObjectId,
                                               qualifiedName,
                                               statement.newObjectId,
                                               statement.newQualifiedName,
                                               newShortName,
                                               context,
                                               connection,
                                               *query};
    workspace_->setExternalWork(true);
    showStatusProgress(tr("Changing %1 %2…").arg(displayKind, qualifiedName),
                       QStringLiteral("navigator"));
    const auto fetchFailure =
        connect(workspace_->adapter(), &EngineAdapter::commandFailed, this,
                [this, query](const QString& error) {
                    if (!pendingObjectAction_ || pendingObjectAction_->query != *query)
                        return;
                    pendingObjectAction_.reset();
                    workspace_->setExternalWork(false);
                    workspace_->adapter()->releaseQuery(*query);
                    showStatus(tr("The object action could not start: %1").arg(error),
                               ToastVariant::Danger, QStringLiteral("navigator"));
                });
    workspace_->adapter()->fetchPage(*query);
    disconnect(fetchFailure);
}

void MainWindow::handleObjectActionEvent(const BridgeEvent& event) {
    const auto eventKind = bridgeText(event.kind);
    if ((eventKind == QStringLiteral("disconnected") ||
         eventKind == QStringLiteral("connection_failed")) &&
        pendingObjectRefresh_ && pendingObjectRefresh_->connection == event.id) {
        pendingObjectRefresh_.reset();
        showStatus(
            tr("Object changed, but the connection closed before navigator refresh completed. "
               "Reconnect and choose Refresh to retry."),
            ToastVariant::Warning, QStringLiteral("navigator"));
    }
    if (eventKind == QStringLiteral("metadata") || eventKind == QStringLiteral("metadata_failed")) {
        if (pendingObjectRefresh_ && pendingObjectRefresh_->connection == event.id &&
            pendingObjectRefresh_->parentObjectId == bridgeText(event.parent) &&
            pendingObjectRefresh_->token == event.request_token) {
            if (eventKind == QStringLiteral("metadata_failed"))
                showStatus(tr("Object changed, but navigator refresh failed: %1. Choose Refresh "
                              "to retry.")
                               .arg(bridgeText(event.error)),
                           ToastVariant::Warning, QStringLiteral("navigator"));
            else
                clearStatus(QStringLiteral("navigator"), tr("Refreshing navigator…"));
            pendingObjectRefresh_.reset();
        }
        return;
    }
    if (!pendingObjectAction_)
        return;
    if ((eventKind == QStringLiteral("disconnected") ||
         eventKind == QStringLiteral("connection_failed")) &&
        event.id == pendingObjectAction_->connection) {
        pendingObjectAction_.reset();
        workspace_->setExternalWork(false);
        showStatus(
            tr("The connection closed before the object action completed. Its outcome is unknown; "
               "reconnect and refresh before retrying."),
            ToastVariant::Danger, QStringLiteral("navigator"));
        return;
    }
    if (event.id != pendingObjectAction_->query || (eventKind != QStringLiteral("query_finished") &&
                                                    eventKind != QStringLiteral("query_failed")))
        return;
    const auto action = *pendingObjectAction_;
    pendingObjectAction_.reset();
    workspace_->adapter()->releaseQuery(action.query);
    workspace_->setExternalWork(false);
    if (eventKind == QStringLiteral("query_failed")) {
        showStatus(tr("Could not %1 %2 %3: %4")
                       .arg(action.action, action.displayKind, action.qualifiedName,
                            bridgeText(event.error)),
                   ToastVariant::Danger, QStringLiteral("navigator"));
        return;
    }
    if (workspace_->driverForConnection(action.connection).isEmpty()) {
        showStatus(tr("Object changed, but the connection is no longer available. Reconnect and "
                      "refresh the navigator."),
                   ToastVariant::Warning, QStringLiteral("navigator"));
        return;
    }
    updatePinsForObjectAction(action);
    for (int i = editors_->count() - 1; i >= 0; --i) {
        auto* object = qobject_cast<ObjectExplorer*>(editors_->widget(i));
        if (!object || object->property("objectProfileId").toString() != action.context ||
            object->property("objectConnection").toULongLong() != action.connection ||
            object->property("objectType").toString() != action.kind ||
            object->property("objectId").toString() != action.objectId)
            continue;
        if (action.action == QStringLiteral("drop")) {
            editors_->removeTab(i);
            object->deleteLater();
        } else {
            const auto pane = object->paneIndex();
            object->setProperty("objectConnection",
                                QVariant::fromValue<qulonglong>(action.connection));
            object->setProperty("objectId", action.newObjectId);
            object->setProperty("objectLabel", action.newQualifiedName);
            object->restoreObject(action.connection, action.newObjectId, action.newQualifiedName,
                                  action.kind);
            object->activateRestoredObject();
            object->selectPane(pane);
            editors_->setTabText(i, objectTabTitle(action.newObjectId, action.newQualifiedName));
        }
    }
    if (!editors_->count())
        showScreen(Screen::Start);
    if (recovery_)
        recovery_->changed();
    showToast(action.action == QStringLiteral("drop")
                  ? tr("%1 %2 dropped.").arg(action.displayKind, action.qualifiedName)
                  : tr("%1 %2 renamed to %3.")
                        .arg(action.displayKind, action.qualifiedName, action.newQualifiedName),
              ToastVariant::Success);
    showStatusProgress(tr("Refreshing navigator…"), QStringLiteral("navigator"));
    pendingObjectRefresh_ = PendingObjectRefresh{action.connection, 0, action.parentObjectId};
    if (!navigatorController_->model()->refreshObject(action.connection, action.parentObjectId)) {
        pendingObjectRefresh_.reset();
        showStatus(tr("Object changed, but the navigator could not refresh. Choose Refresh to "
                      "retry."),
                   ToastVariant::Warning, QStringLiteral("navigator"));
    }
}
} // namespace choscordb
