#include "app/main_window.h"

#include "app/main_window_ui.h"
#include "app/navigator_controller.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "design_system/button/button.h"
#include "design_system/text/text.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "widgets/editor_completion/editor_completion.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTreeView>
#include <utility>

namespace choscordb {

void MainWindow::connectNavigator(const Ui& ui) {
    const auto newConnection = ui.newConnection;
    const auto refreshNavigator = ui.refreshNavigator;
    const auto disconnectNavigator = ui.disconnectNavigator;
    const auto filter = ui.filter;
    const auto tree = ui.tree;
    const auto objectsEmpty = ui.objectsEmpty;
    const auto navigatorStatus = ui.navigatorStatus;
    const auto connections = ui.connections;
    const auto toast = ui.toast;
    auto* navigatorController = new NavigatorController(workspace_->adapter(), tree, filter, this);
    navigatorController_ = navigatorController;
    const auto updateObjectsEmpty = [this, tree, filter, objectsEmpty] {
        objectsEmpty->setVisible(tree->model()->rowCount() == 0);
        objectsEmpty->setText(
            !browsingConnection_ ? tr("No database selected.\n\nSelect a connection to browse its "
                                      "schemas and objects.")
            : !filter->text().isEmpty()
                ? tr("No matching objects.\n\nTry a different filter or clear the search.")
                : tr("No objects to show.\n\nRefresh to check for schemas and objects."));
    };
    connect(tree->model(), &QAbstractItemModel::rowsInserted, objectsEmpty, updateObjectsEmpty);
    connect(tree->model(), &QAbstractItemModel::rowsRemoved, objectsEmpty, updateObjectsEmpty);
    connect(tree->model(), &QAbstractItemModel::modelReset, objectsEmpty, updateObjectsEmpty);
    connect(tree->model(), &QAbstractItemModel::layoutChanged, objectsEmpty, updateObjectsEmpty);
    connect(filter, &QLineEdit::textChanged, objectsEmpty, updateObjectsEmpty);
    connect(navigatorController, &NavigatorController::searchStatusChanged, objectsEmpty,
            updateObjectsEmpty);
    updateObjectsEmpty();
    connect(navigatorController, &NavigatorController::ddlRequested, this,
            [this](quint64 connection, const QString& objectId, const QString& label,
                   const QString& kind, const QVariantList& properties) {
                openObjectTab(connection, objectId, label, kind, properties, 3);
            });
    connect(navigatorController, &NavigatorController::searchStatusChanged, navigatorStatus,
            [this, navigatorStatus](const QString& status) {
                navigatorStatus->setText(
                    status.isEmpty()
                        ? (browsingConnection_ ? tr("● Connected") : tr("○ Disconnected"))
                        : status);
            });
    auto* refreshNavigatorAction = new QAction(tr("Refresh selected object"), tree);
    refreshNavigatorAction->setObjectName("navigatorRefreshAction");
    refreshNavigatorAction->setEnabled(false);
    auto* disconnectNavigatorAction = new QAction(tr("Disconnect selected session"), tree);
    disconnectNavigatorAction->setObjectName("navigatorDisconnectAction");
    disconnectNavigatorAction->setEnabled(false);
    tree->addAction(newConnection);
    tree->addAction(refreshNavigatorAction);
    tree->addAction(disconnectNavigatorAction);
    connect(refreshNavigatorAction, &QAction::triggered, navigatorController,
            &NavigatorController::refreshCurrent);
    connect(disconnectNavigatorAction, &QAction::triggered, navigatorController,
            &NavigatorController::disconnectCurrent);
    connect(refreshNavigator, &QPushButton::clicked, refreshNavigatorAction, &QAction::trigger);
    connect(disconnectNavigator, &QPushButton::clicked, disconnectNavigatorAction,
            &QAction::trigger);
    connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this, tree](const QModelIndex& current, const QModelIndex& previous) {
                if (!current.isValid())
                    return;
                if (!allowDocumentChange()) {
                    const QSignalBlocker blocker(tree->selectionModel());
                    tree->selectionModel()->setCurrentIndex(
                        previous, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                    return;
                }
                auto object = current;
                auto kind = object.data(NavigatorModel::KindRole).toString();
                const auto selectedKind = kind;
                while (object.isValid() && kind != "table" && kind != "view" && kind != "index" &&
                       kind != "sequence" && kind != "function" && kind != "schema" &&
                       kind != "connection") {
                    object = object.parent();
                    kind = object.data(NavigatorModel::KindRole).toString();
                }
                if (!object.isValid())
                    return;
                const auto connection = object.data(NavigatorModel::ConnectionRole);
                if (!connection.isValid())
                    return;
                browsingConnection_ = connection.toULongLong();
                emit browsingConnectionChanged(*browsingConnection_);
                if (kind == "table" || kind == "view" || kind == "index" || kind == "sequence" ||
                    kind == "function") {
                    const auto pane = selectedKind == "column"       ? 0
                                      : selectedKind == "index"      ? 1
                                      : selectedKind.contains("key") ? 2
                                      : selectedKind == "ddl"        ? 3
                                      : selectedKind == "data"       ? 4
                                      : selectedKind == "table"      ? 4
                                                                     : -1;
                    openObjectTab(*browsingConnection_,
                                  object.data(NavigatorModel::ObjectIdRole).toString(),
                                  object.data(NavigatorModel::QualifiedNameRole).toString(), kind,
                                  object.data(NavigatorModel::PropertiesRole).toList(), pane);
                }
            });
    connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [tree, refreshNavigator, disconnectNavigator, refreshNavigatorAction,
             disconnectNavigatorAction](const QModelIndex&) {
                const auto current = tree->currentIndex();
                const bool canRefresh = current.isValid();
                const bool canDisconnect =
                    current.data(NavigatorModel::KindRole).toString() == "connection";
                refreshNavigator->setEnabled(canRefresh);
                disconnectNavigator->setEnabled(canDisconnect);
                refreshNavigatorAction->setEnabled(canRefresh);
                disconnectNavigatorAction->setEnabled(canDisconnect);
                tree->setAccessibleDescription(
                    current.isValid()
                        ? QObject::tr("Context actions are available in the navigator header.")
                        : QString{});
            });
    connect(navigatorController, &NavigatorController::disconnectRequested, workspace_,
            &QueryWorkspace::disconnectConnection);
    connect(navigatorController, &NavigatorController::generationFailed, this,
            [toast](const QString& error) {
                toast->showToast(tr("Error"), error, ToastVariant::Danger);
            });
    const auto openGeneratedSql = [this, connections](quint64 connection, const QString& sql) {
        if (databaseClosePending_ || !editors_->isEnabled() ||
            (recovery_ && (!recovery_->isReady() || recovery_->isClosing())))
            return;
        const int target = connections->findData(QVariant::fromValue<qulonglong>(connection));
        if (target < 0) {
            showToast(tr("The selected connection is no longer available."), ToastVariant::Danger);
            return;
        }
        if (target != connections->currentIndex() && !connections->isEnabled()) {
            showToast(tr("Finish the active query before switching connections to generate SQL."),
                      ToastVariant::Warning);
            return;
        }
        const auto bytes = sql.toUtf8();
        if (!sql.isValidUtf16() || bytes.size() > DocumentIo::MaximumBytes) {
            showToast(tr("Generated SQL exceeds editor limits."), ToastVariant::Danger);
            return;
        }
        auto* editor = addEditor();
        if (!editor)
            return;
        if (!editor->restoreDocument(bytes, {}, 0, 0, true)) {
            editors_->removeTab(editors_->indexOf(editor));
            editor->deleteLater();
            showToast(tr("Generated SQL could not be opened."), ToastVariant::Danger);
            return;
        }
        connections->setCurrentIndex(target);
        editor->setConnectionTarget(connection, connections->itemText(target));
        workspace_->documentChanged();
        editor->setProfileId(workspace_->profileIdForConnection(connection));
        editor->setProperty("documentTitle", tr("Generated SQL %1").arg(nextDocumentNumber_));
        editors_->setTabText(editors_->indexOf(editor),
                             editor->property("documentTitle").toString() + " •");
        editor->setFocus();
        toast_->showToast(tr("SQL generated"), tr("Review the draft before running."),
                          ToastVariant::Success);
        if (recovery_)
            recovery_->changed();
    };
    connect(navigatorController, &NavigatorController::sqlGenerated, this, openGeneratedSql);
    openGeneratedSql_ = openGeneratedSql;

    auto rebuildCompletion = [this, connections, model = navigatorController->model()] {
        if (!connections->currentData().isValid()) {
            completion_->setCatalog(CompletionService{});
            return;
        }
        const auto limits = CompletionService::limits();
        auto snapshot =
            model->completionSnapshot(connections->currentData().toULongLong(),
                                      limits.maxMetadataEntries, limits.maxMetadataBytes);
        QList<CompletionCandidate> items;
        items.reserve(static_cast<qsizetype>(snapshot.objects.size()));
        for (auto& object : snapshot.objects)
            items.append(
                {std::move(object.name), std::move(object.qualifiedName), std::move(object.kind)});
        completion_->setCatalog(CompletionService(std::move(items), snapshot.partial));
    };
    connect(connections, &QComboBox::currentIndexChanged, this, rebuildCompletion);
    connect(workspace_, &QueryWorkspace::documentTargetChanged, this, rebuildCompletion);
    connect(navigatorController->model(), &NavigatorModel::completionChanged, this,
            [connections, rebuildCompletion](quint64 id) {
                if (id == connections->currentData().toULongLong())
                    rebuildCompletion();
            });
    rebuildCompletion();
    connect(workspace_, &QueryWorkspace::connectionReady, navigatorController,
            [navigatorController, connections](quint64 id) {
                navigatorController->addConnection(id, connections->itemText(connections->findData(
                                                           QVariant::fromValue<qulonglong>(id))));
            });
    connect(workspace_, &QueryWorkspace::connectionReady, this, [this](quint64 id) {
        const auto profileId = workspace_->profileIdForConnection(id);
        if (profileId.isEmpty())
            return;
        for (int i = 0; i < editors_->count(); ++i) {
            auto* object = qobject_cast<ObjectExplorer*>(editors_->widget(i));
            if (!object || !object->needsConnection())
                continue;
            const auto context = object->property("objectProfileId").toString();
            if (context != QStringLiteral("profile:%1").arg(profileId))
                continue;
            object->setProperty("objectProfileId", QStringLiteral("profile:%1").arg(profileId));
            object->setProperty("objectConnection", QVariant::fromValue<qulonglong>(id));
            object->restoreObject(id, object->property("objectId").toString(),
                                  object->property("objectLabel").toString(),
                                  object->property("objectType").toString());
            if (editors_->currentWidget() == object)
                object->activateRestoredObject();
        }
    });
}
} // namespace choscordb
