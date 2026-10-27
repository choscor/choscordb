#include "app/main_window.h"
#include "widgets/sidebar_section/sidebar_section.h"

#include "app/navigator_controller.h"
#include "app/object_action_sql.h"
#include "app/object_data_workspace.h"
#include "app/object_explorer.h"
#include "app/object_kind_icon.h"
#include "app/object_tab_title.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "bridge/rust_text.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/text/text.h"
#include "models/navigator_model.h"
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTabWidget>
#include <QVBoxLayout>

namespace choscordb {
namespace {
using bridge_detail::fromRust;
} // namespace

bool MainWindow::objectActionReady(quint64 connection, const QString& objectId, const QString& kind,
                                   const QString& parentObjectId, const QString& qualifiedName,
                                   const QString& relationSubtype) {
    if (pendingObjectAction_ || pendingObjectRefresh_) {
        reportNotice(tr("Finish the current object action and navigator refresh before trying "
                        "again."));
        return false;
    }
    if (databaseClosePending_ || !workspace_ || !navigatorController_ ||
        !workspace_->liveConnectionLabel(connection) ||
        workspace_->driverForConnection(connection).isEmpty()) {
        reportError(tr("The selected connection is no longer available."));
        return false;
    }
    if (!navigatorController_->model()->matchesObject(connection, objectId, kind, qualifiedName,
                                                      parentObjectId, relationSubtype)) {
        reportNotice(tr("The selected object changed. %1")
                         .arg(EngineAdapter::recoveryHint(RecoveryHint::RefreshNavigator)));
        return false;
    }
    if (!workspace_->navigationAllowed() || workspace_->hasPendingEdits() ||
        workspace_->activeTransaction(connection)) {
        reportNotice(tr("Finish or cancel active database work and pending edits before changing "
                        "this object."));
        return false;
    }
    for (int i = 0; i < editors_->count(); ++i) {
        auto* object = qobject_cast<ObjectExplorer*>(editors_->widget(i));
        if (!object || object->property("objectConnection").toULongLong() != connection)
            continue;
        if (object->operationInFlight()) {
            reportNotice(tr("Finish the active object inspection before changing this object."));
            return false;
        }
        if (auto* objectData = object->findChild<ObjectDataWorkspace*>())
            for (auto* result : objectData->findChildren<QueryWorkspace*>())
                if (!result->navigationAllowed() || result->hasPendingEdits()) {
                    reportNotice(tr("Finish or cancel active object data work and pending edits "
                                    "before changing this object."));
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
    const auto displayKind = relationKindPhrase(kind, relationSubtype);
    ObjectActionStatement statement;
    QString newShortName;
    if (action == QStringLiteral("drop")) {
        statement = ObjectActionSql::drop(driver, kind, objectId, qualifiedName, relationSubtype);
        if (!statement.valid) {
            reportError(statement.error);
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
        auto* sections = dialog.createSections();
        auto* heading = new design::Text(dialog.windowTitle(), sections);
        heading->setTextStyle(design::TextStyle::Headline);
        sections->headerLayout()->addWidget(heading);
        auto* dismiss = sections->addDismissButton(tr("Close rename dialog"));
        dismiss->setObjectName("renameObjectDismiss");
        connect(dismiss, &QPushButton::clicked, &dialog, &QDialog::reject);
        auto* layout = sections->bodyLayout();
        layout->setSpacing(design::spacing(design::Spacing::Two));
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
        preview->setProperty("designRole", "codePreview");
        preview->setFont(
            design::resolveTypography(design::TextStyle::Body, design::FontDesign::Monospaced));
        preview->setFrameShape(QFrame::NoFrame);
        layout->addWidget(preview);
        auto* status = dialog.createInlineStatus(&dialog);
        status->setObjectName("renameObjectValidation");
        layout->addWidget(status);
        auto* buttons = sections->footerLayout();
        auto* confirm = new design::Button(tr("Rename"), &dialog);
        confirm->setObjectName("renameObjectConfirm");
        confirm->setDefault(true);
        auto* cancel = new design::Button(tr("Cancel"), &dialog);
        cancel->setObjectName("renameObjectCancel");
        cancel->setVariant(design::ButtonVariant::Outline);
        buttons->addStretch();
        buttons->addWidget(cancel);
        buttons->addWidget(confirm);
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
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
            reportError(statement.error);
        return;
    }
    const auto profileId = workspace_->profileIdForConnection(connection);
    const auto context = EngineAdapter::objectTabContext(profileId, connection);
    const auto query = workspace_->adapter()->execute(connection, statement.sql);
    if (!query) {
        reportError(tr("The object action could not be submitted. %1")
                        .arg(EngineAdapter::recoveryHint(RecoveryHint::CheckConnection)));
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
    syncObjectSectionBusy();
    workspace_->setExternalWork(true);
    const auto fetchFailure =
        connect(workspace_->adapter(), &EngineAdapter::commandFailed, this,
                [this, query](const QString& error) {
                    if (!pendingObjectAction_ || pendingObjectAction_->query != *query)
                        return;
                    pendingObjectAction_.reset();
                    syncObjectSectionBusy();
                    workspace_->setExternalWork(false);
                    workspace_->adapter()->releaseQuery(*query);
                    reportError(tr("The object action could not start: %1").arg(error));
                });
    workspace_->adapter()->fetchPage(*query);
    disconnect(fetchFailure);
}

void MainWindow::syncObjectSectionBusy() {
    if (pendingObjectAction_ || pendingObjectRefresh_)
        objectSection_->setBusy(true, tr("Changing object…"));
    else
        objectSection_->setBusy(objectSearchBusy_, tr("Searching objects…"));
}

void MainWindow::handleObjectActionEvent(const BridgeEvent& event) {
    const bool wasPending = pendingObjectAction_ || pendingObjectRefresh_;
    const auto settle = qScopeGuard([this, wasPending] {
        if (wasPending)
            syncObjectSectionBusy();
    });
    const auto eventKind = fromRust(event.kind);
    if ((eventKind == QStringLiteral("disconnected") ||
         eventKind == QStringLiteral("connection_failed")) &&
        pendingObjectRefresh_ && pendingObjectRefresh_->connection == event.id) {
        pendingObjectRefresh_.reset();
        reportError(
            tr("Object changed, but the navigator did not refresh. %1")
                .arg(EngineAdapter::recoveryHint(RecoveryHint::ReconnectAndRefreshNavigator)));
    }
    if (eventKind == QStringLiteral("metadata") || eventKind == QStringLiteral("metadata_failed")) {
        if (pendingObjectRefresh_ && pendingObjectRefresh_->connection == event.id &&
            pendingObjectRefresh_->parentObjectId == fromRust(event.parent) &&
            pendingObjectRefresh_->token == event.request_token) {
            if (eventKind == QStringLiteral("metadata_failed"))
                reportError(tr("Object changed, but the navigator did not refresh. %1")
                                .arg(EngineAdapter::rowStatus(RowStatusCause::ChildrenFailed,
                                                              fromRust(event.error))
                                         .message));
            else
                showToast(pendingObjectRefresh_->success, ToastVariant::Success);
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
        reportError(
            tr("The connection closed before the object action completed. Its outcome is unknown. "
               "%1")
                .arg(EngineAdapter::recoveryHint(RecoveryHint::ReconnectAndRefresh)));
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
        reportError(tr("Could not %1 %2 %3: %4")
                        .arg(action.action, action.displayKind, action.qualifiedName,
                             fromRust(event.error)));
        return;
    }
    if (workspace_->driverForConnection(action.connection).isEmpty()) {
        reportError(
            tr("Object changed, but the connection is no longer available. %1")
                .arg(EngineAdapter::recoveryHint(RecoveryHint::ReconnectAndRefreshNavigator)));
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
    pendingObjectRefresh_ = PendingObjectRefresh{
        action.connection, 0, action.parentObjectId,
        action.action == QStringLiteral("drop")
            ? tr("%1 %2 dropped.").arg(action.displayKind, action.qualifiedName)
            : tr("%1 %2 renamed to %3.")
                  .arg(action.displayKind, action.qualifiedName, action.newQualifiedName)};
    if (!navigatorController_->model()->refreshObject(action.connection, action.parentObjectId)) {
        pendingObjectRefresh_.reset();
        reportError(tr("Object changed, but the navigator did not refresh. %1")
                        .arg(EngineAdapter::recoveryHint(RecoveryHint::RefreshNavigator)));
    }
}
} // namespace choscordb
