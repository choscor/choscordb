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
#include "bridge/request_token.h"
#include "bridge/rust_text.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/field/field.h"
#include "design_system/metrics/metrics.h"
#include "design_system/text/text.h"
#include "models/navigator_model.h"
#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
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
}

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

std::optional<ObjectActionStatement>
MainWindow::execObjectActionForm(const ObjectActionRequest& base, const QString& shortName,
                                 const QString& displayKind) {
    const auto& action = base.action;
    const bool named = action != QStringLiteral("truncate");
    const auto truncate = ObjectActionSql::truncateTraits(base.driver);
    DialogShell dialog(this);
    dialog.setObjectName(action + "ObjectDialog");
    dialog.resize(design::dialogInitialSize(design::DialogSize::Short));
    const QString title = action == QStringLiteral("rename")  ? tr("Rename %1").arg(displayKind)
                          : action == QStringLiteral("clone") ? tr("Clone %1").arg(displayKind)
                                                              : tr("Truncate %1").arg(displayKind);
    dialog.setWindowTitle(title);
    auto* sections = dialog.createSections();
    auto* heading = new design::Text(title, sections);
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss = sections->addDismissButton(tr("Close %1 dialog").arg(title));
    dismiss->setObjectName(action + "ObjectDismiss");
    connect(dismiss, &QPushButton::clicked, &dialog, &QDialog::reject);
    auto* layout = sections->bodyLayout();
    const QString description =
        action == QStringLiteral("rename") ? tr("Rename %1 %2 in the same schema or database.")
        : action == QStringLiteral("clone")
            ? tr("Copy %1 %2 to a new table in the same schema or database.")
            : tr("Delete all rows from %1 %2. You cannot undo this.");
    layout->addWidget(
        dialog.createDescription(description.arg(displayKind, base.qualifiedName), &dialog));
    auto* name = new QLineEdit(&dialog);
    name->setObjectName(action + "ObjectName");
    name->setAccessibleName(tr("New unqualified name"));
    name->setText(action == QStringLiteral("clone") ? tr("%1_copy").arg(shortName) : shortName);
    auto* validation = new design::FieldValidation(name, &dialog);
    validation->setObjectName(action + "ObjectValidation");
    auto* form = sections->addForm();
    if (named)
        sections->addFormRow(form, tr("New name"), validation);
    else
        validation->hide();
    QList<std::pair<QCheckBox*, bool ObjectActionRequest::*>> options;
    const auto addOption = [&](const QString& label, const char* objectName,
                               bool ObjectActionRequest::* field) {
        auto* option = new QCheckBox(label, &dialog);
        option->setObjectName(objectName);
        layout->addWidget(option);
        options.append({option, field});
    };
    if (action == QStringLiteral("clone"))
        addOption(tr("Copy rows"), "cloneObjectData", &ObjectActionRequest::withData);
    if (action == QStringLiteral("truncate") && truncate.restartIdentity)
        addOption(tr("Restart identity sequences"), "truncateObjectRestartIdentity",
                  &ObjectActionRequest::restartIdentity);
    if (action == QStringLiteral("truncate") && truncate.cascade)
        addOption(tr("Also truncate tables that reference this table"), "truncateObjectCascade",
                  &ObjectActionRequest::cascade);
    auto* error = new QLabel(&dialog);
    error->setObjectName(action + "ObjectError");
    error->setProperty("designRole", "fieldError");
    error->setWordWrap(true);
    error->hide();
    layout->addWidget(error);
    layout->addWidget(dialog.createDescription(tr("SQL to execute"), &dialog));
    auto* preview = new QPlainTextEdit(&dialog);
    preview->setObjectName(action + "ObjectSql");
    preview->setAccessibleName(tr("SQL preview"));
    preview->setReadOnly(true);
    preview->setProperty("designRole", "codePreview");
    preview->setFont(
        design::resolveTypography(design::TextStyle::Body, design::FontDesign::Monospaced));
    preview->setFrameShape(QFrame::NoFrame);
    layout->addWidget(preview);
    auto* cancel = sections->addAction(design::DialogActionRole::Dismiss, tr("Cancel"));
    cancel->setObjectName(action + "ObjectCancel");
    auto* confirm =
        named ? sections->addAction(design::DialogActionRole::Primary,
                                    action == QStringLiteral("rename") ? tr("Rename") : tr("Clone"))
              : sections->addAction(design::DialogActionRole::Destructive, title);
    confirm->setObjectName(action + "ObjectConfirm");
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    const auto current = [&] {
        auto request = base;
        if (named)
            request.newName = name->text();
        for (const auto& [option, field] : options)
            request.*field = option->isChecked();
        return ObjectActionSql::prepare(request);
    };
    ObjectActionStatement statement;
    const auto update = [&] {
        statement = current();
        preview->setPlainText(statement.sql);
        const auto message = statement.valid ? QString{} : statement.error;
        if (named) {
            validation->setError(message);
        } else {
            error->setText(message);
            error->setVisible(!message.isEmpty());
        }
    };
    connect(confirm, &QPushButton::clicked, &dialog, [&] {
        update();
        if (statement.valid)
            dialog.accept();
        else if (named)
            name->setFocus(Qt::OtherFocusReason);
    });
    connect(name, &QLineEdit::textChanged, &dialog, update);
    for (const auto& [option, field] : options)
        connect(option, &QCheckBox::toggled, &dialog, update);
    update();
    if (named) {
        name->setFocus();
        name->selectAll();
    }
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    statement = current();
    if (!statement.valid) {
        reportError(statement.error);
        return std::nullopt;
    }
    return statement;
}

void MainWindow::requestObjectAction(const QString& action, quint64 connection,
                                     const QString& objectId, const QString& shortName,
                                     const QString& kind, const QString& parentObjectId,
                                     const QString& qualifiedName, const QString& relationSubtype) {
    if (!objectActionReady(connection, objectId, kind, parentObjectId, qualifiedName,
                           relationSubtype))
        return;
    const auto displayKind = relationKindPhrase(kind, relationSubtype);
    const ObjectActionRequest request{.action = action,
                                      .driver = workspace_->driverForConnection(connection),
                                      .kind = kind,
                                      .objectId = objectId,
                                      .qualifiedName = qualifiedName,
                                      .relationSubtype = relationSubtype};
    std::optional<ObjectActionStatement> statement;
    if (action == QStringLiteral("drop")) {
        statement = ObjectActionSql::prepare(request);
        if (!statement->valid) {
            reportError(statement->error);
            return;
        }
        ConfirmationDialog dialog(QMessageBox::Warning, tr("Drop %1").arg(displayKind),
                                  tr("Drop %1 %2?\n\nSQL to execute:\n%3")
                                      .arg(displayKind, qualifiedName, statement->sql),
                                  QMessageBox::Cancel, this);
        dialog.setObjectName("dropObjectDialog");
        dialog.setTextFormat(Qt::PlainText);
        auto* confirm = dialog.addActionButton(tr("Drop %1").arg(displayKind),
                                               ConfirmationDialog::Consequence::Destructive);
        confirm->setObjectName("dropObjectConfirm");
        dialog.exec();
        if (dialog.clickedButton() != confirm)
            return;
    } else {
        statement = execObjectActionForm(request, shortName, displayKind);
        if (!statement)
            return;
    }
    if (!objectActionReady(connection, objectId, kind, parentObjectId, qualifiedName,
                           relationSubtype))
        return;
    std::vector<ReviewedEditStatement> batch;
    for (const auto& sql : statement->statements)
        batch.push_back({.sql = sql});
    const auto profileId = workspace_->profileIdForConnection(connection);
    const auto token = nextRequestToken();
    pendingObjectAction_ =
        PendingObjectAction{action,
                            objectId,
                            kind,
                            displayKind,
                            parentObjectId,
                            qualifiedName,
                            statement->newObjectId,
                            statement->newQualifiedName,
                            statement->newName,
                            EngineAdapter::objectTabContext(profileId, connection),
                            connection,
                            token};
    QString submitError;
    const auto failure = connect(workspace_->adapter(), &EngineAdapter::commandFailed, this,
                                 [&submitError](const QString& error) { submitError = error; });
    const bool submitted = workspace_->adapter()->applyEditBatch(connection, batch, token);
    disconnect(failure);
    if (!submitted) {
        pendingObjectAction_.reset();
        reportError(tr("The object action could not start: %1").arg(submitError));
        return;
    }
    syncObjectSectionBusy();
    workspace_->setExternalWork(true);
}

void MainWindow::copyObjectScript(const QString& script, quint64 connection,
                                  const QString& objectId, const QString& kind,
                                  const QString& qualifiedName, const QString& relationSubtype) {
    const auto driver = workspace_->driverForConnection(connection);
    if (driver.isEmpty()) {
        reportError(tr("The selected connection is no longer available."));
        return;
    }
    const auto result =
        ObjectActionSql::script(script, driver, kind, objectId, qualifiedName, relationSubtype);
    if (!result.valid) {
        reportError(result.error);
        return;
    }
    if (!result.fromDdl) {
        QGuiApplication::clipboard()->setText(result.sql);
        toast_->showToast(tr("Script copied"), qualifiedName, ToastVariant::Success);
        return;
    }
    const auto token = nextRequestToken();
    pendingObjectScript_ = PendingObjectScript{connection, token, objectId, qualifiedName};
    if (!workspace_->adapter()->objectDdl(connection, objectId, token))
        pendingObjectScript_.reset();
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
                toast_->showToast(pendingObjectRefresh_->successTitle,
                                  pendingObjectRefresh_->success, ToastVariant::Success);
            pendingObjectRefresh_.reset();
        }
        return;
    }
    if (pendingObjectScript_ && event.id == pendingObjectScript_->connection &&
        event.request_token == pendingObjectScript_->token &&
        (eventKind == QStringLiteral("ddl") || eventKind == QStringLiteral("ddl_failed"))) {
        const auto script = *std::exchange(pendingObjectScript_, std::nullopt);
        if (eventKind == QStringLiteral("ddl_failed")) {
            reportError(tr("Could not copy the script for %1: %2")
                            .arg(script.qualifiedName, fromRust(event.error)));
            return;
        }
        QGuiApplication::clipboard()->setText(fromRust(event.ddl));
        toast_->showToast(tr("Script copied"), script.qualifiedName, ToastVariant::Success);
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
    if (event.id != pendingObjectAction_->connection ||
        event.request_token != pendingObjectAction_->token ||
        (eventKind != QStringLiteral("edit_applied") && eventKind != QStringLiteral("edit_failed")))
        return;
    const auto action = *pendingObjectAction_;
    pendingObjectAction_.reset();
    workspace_->setExternalWork(false);
    if (eventKind == QStringLiteral("edit_failed")) {
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
    const bool drop = action.action == QStringLiteral("drop");
    const bool rename = action.action == QStringLiteral("rename");
    if (drop || rename)
        updatePinsForObjectAction(action);
    for (int i = editors_->count() - 1; (drop || rename) && i >= 0; --i) {
        auto* object = qobject_cast<ObjectExplorer*>(editors_->widget(i));
        if (!object || object->property("objectProfileId").toString() != action.context ||
            object->property("objectConnection").toULongLong() != action.connection ||
            object->property("objectType").toString() != action.kind ||
            object->property("objectId").toString() != action.objectId)
            continue;
        if (drop) {
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
    const auto [title, message] =
        drop ? std::pair{tr("Object dropped"),
                         tr("%1 %2 dropped.").arg(action.displayKind, action.qualifiedName)}
        : rename
            ? std::pair{tr("Object renamed"),
                        tr("%1 %2 renamed to %3.")
                            .arg(action.displayKind, action.qualifiedName, action.newQualifiedName)}
        : action.action == QStringLiteral("clone")
            ? std::pair{tr("Object cloned"),
                        tr("%1 %2 cloned to %3.")
                            .arg(action.displayKind, action.qualifiedName, action.newQualifiedName)}
            : std::pair{tr("Object truncated"),
                        tr("%1 %2 truncated.").arg(action.displayKind, action.qualifiedName)};
    pendingObjectRefresh_ =
        PendingObjectRefresh{action.connection, 0, action.parentObjectId, title, message};
    if (!navigatorController_->model()->refreshObject(action.connection, action.parentObjectId)) {
        pendingObjectRefresh_.reset();
        reportError(tr("Object changed, but the navigator did not refresh. %1")
                        .arg(EngineAdapter::recoveryHint(RecoveryHint::RefreshNavigator)));
    }
}
}
