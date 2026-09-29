#include "app/main_window.h"

#include "app/editor_preferences.h"
#include "app/main_window_ui.h"
#include "app/main_window_widgets.h"
#include "app/navigator_controller.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "design_system/button/button.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
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
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QTreeView>
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

namespace choscordb {

void MainWindow::connectNavigator(const Ui& ui) {
    const auto newConnection = ui.newConnection;
    const auto refreshNavigator = ui.refreshNavigator;
    const auto filter = ui.filter;
    const auto tree = ui.tree;
    const auto objectsEmpty = ui.objectsEmpty;
    const auto navigatorStatus = ui.navigatorStatus;
    const auto connections = ui.connections;
    const auto toast = ui.toast;
    auto* navigatorController = new NavigatorController(workspace_->adapter(), tree, filter, this);
    navigatorController_ = navigatorController;
    auto* connectionsScroll = findChild<QScrollArea*>("connectionsScroll");
    const auto revealCurrent = [tree, connectionsScroll] {
        const auto current = tree->currentIndex();
        if (!connectionsScroll || !current.isValid())
            return;
        const auto row = tree->visualRect(current);
        const auto point = tree->viewport()->mapTo(connectionsScroll->widget(), row.center());
        connectionsScroll->ensureVisible(point.x(), point.y(), 0, row.height());
    };
    const auto updateTreeHeight = [this, tree] {
        const auto* model = tree->model();
        if (!model)
            return;
        const std::function<int(const QModelIndex&)> visibleHeight =
            [this, tree, model, &visibleHeight](const QModelIndex& parent) -> int {
            int height = 0;
            for (int row = 0; row < model->rowCount(parent); ++row) {
                const auto index = model->index(row, 0, parent);
                height += std::max(theme_->metrics().navigationRowHeight,
                                   tree->sizeHintForIndex(index).height());
                if (tree->isExpanded(index))
                    height += visibleHeight(index);
            }
            return height;
        };
        tree->setFixedHeight(std::max(design::spacing(design::Spacing::Two),
                                      visibleHeight({}) + 2 * tree->frameWidth()));
    };
    const auto scheduleTreeHeight = [tree, updateTreeHeight] {
        QTimer::singleShot(0, tree, updateTreeHeight);
    };
    new main_window_detail::SidebarWidthObserver(tree->viewport(), scheduleTreeHeight);
    const auto scheduleRevealCurrent = [tree, revealCurrent] {
        QTimer::singleShot(0, tree, revealCurrent);
    };
    auto* treeModel = tree->model();
    connect(treeModel, &QAbstractItemModel::rowsInserted, tree, scheduleTreeHeight);
    connect(treeModel, &QAbstractItemModel::rowsRemoved, tree, scheduleTreeHeight);
    connect(treeModel, &QAbstractItemModel::modelReset, tree, scheduleTreeHeight);
    connect(treeModel, &QAbstractItemModel::layoutChanged, tree, scheduleTreeHeight);
    connect(treeModel, &QAbstractItemModel::dataChanged, tree, scheduleTreeHeight);
    connect(tree, &QTreeView::expanded, tree, updateTreeHeight);
    connect(tree, &QTreeView::collapsed, tree, updateTreeHeight);
    connect(theme_, &design::ThemeManager::metricsChanged, tree, scheduleTreeHeight);
    connect(navigatorController, &NavigatorController::selectedConnectionsChanged, tree,
            scheduleTreeHeight);
    connect(filter, &QLineEdit::textChanged, tree, scheduleTreeHeight);
    connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, tree,
            scheduleRevealCurrent);
    scheduleTreeHeight();
    navigatorController->setDriverResolver(
        [this](quint64 connection) { return workspace_->driverForConnection(connection); });
    constexpr quint64 visibilityLoadToken = quint64(1) << 58;
    auto pendingVisibilityLoad = std::make_shared<bool>(true);
    connect(preferences_, &EditorPreferencesController::systemSchemaVisibilitySaved, this,
            [navigatorController, pendingVisibilityLoad](bool visible) {
                *pendingVisibilityLoad = false;
                navigatorController->setShowSystemSchemas(visible);
            });
    connect(workspace_->adapter(), &EngineAdapter::queryPreferencesReady, this,
            [navigatorController, pendingVisibilityLoad](quint64 token,
                                                         const QueryPreferences& preferences) {
                if (token != visibilityLoadToken || !*pendingVisibilityLoad)
                    return;
                *pendingVisibilityLoad = false;
                navigatorController->setShowSystemSchemas(preferences.showSystemSchemas);
            });
    workspace_->adapter()->getQueryPreferences(visibilityLoadToken);
    connect(navigatorController, &NavigatorController::objectActionRequested, this,
            &MainWindow::requestObjectAction);
    connect(workspace_->adapter(), &EngineAdapter::eventReady, this,
            &MainWindow::handleObjectActionEvent, Qt::DirectConnection);
    connect(navigatorController->model(), &NavigatorModel::childrenRequested, this,
            [this](quint64 connection, const QString& parent, quint64 token) {
                if (pendingObjectRefresh_ && pendingObjectRefresh_->connection == connection &&
                    pendingObjectRefresh_->parentObjectId == parent)
                    pendingObjectRefresh_->token = token;
            });
    connect(workspace_->adapter(), &EngineAdapter::metadataSubmissionFailed, this,
            [this](quint64 connection, const QString& parent, quint64 token, const QString& error) {
                if (!pendingObjectRefresh_ || pendingObjectRefresh_->connection != connection ||
                    pendingObjectRefresh_->parentObjectId != parent ||
                    (pendingObjectRefresh_->token != 0 && pendingObjectRefresh_->token != token))
                    return;
                pendingObjectRefresh_.reset();
                showToast(
                    tr("Object changed, but navigator refresh failed: %1. Choose Refresh to retry.")
                        .arg(error),
                    ToastVariant::Danger);
            });
    const auto updateObjectsEmpty = [this, tree, filter, objectsEmpty] {
        objectsEmpty->setVisible(tree->model()->rowCount() == 0);
        objectsEmpty->setText(
            selectedProfileIds_.isEmpty()
                ? tr("No database selected.\nSelect a connection to browse its "
                     "schemas and objects.")
            : !filter->text().isEmpty()
                ? tr("No matching objects.\nTry a different filter or clear the search.")
                : tr("No objects to show.\nRefresh to check for schemas and objects."));
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
            [navigatorStatus](const QString& status) {
                navigatorStatus->setText(status);
                navigatorStatus->setProperty("state", status.isEmpty() ? "" : "search");
                navigatorStatus->setAccessibleName(
                    status.isEmpty() ? QString() : tr("Navigator search status: %1").arg(status));
                navigatorStatus->style()->unpolish(navigatorStatus);
                navigatorStatus->style()->polish(navigatorStatus);
                navigatorStatus->setVisible(!status.isEmpty());
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
                activateNavigatorObject(current);
            });
    connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [tree, refreshNavigator, refreshNavigatorAction,
             disconnectNavigatorAction](const QModelIndex&) {
                const auto current = tree->currentIndex();
                const bool canRefresh = current.isValid();
                const bool canDisconnect =
                    current.data(NavigatorModel::KindRole).toString() == "connection";
                refreshNavigator->setEnabled(canRefresh);
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

void MainWindow::activateNavigatorObject(const QModelIndex& current) {
    if (!current.isValid())
        return;
    auto object = current;
    auto kind = object.data(NavigatorModel::KindRole).toString();
    const auto selectedKind = kind;
    while (object.isValid() && kind != "table" && kind != "view" && kind != "index" &&
           kind != "sequence" && kind != "function" && kind != "schema" && kind != "connection") {
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
        auto* tree = findChild<QTreeView*>("databaseNavigator");
        const auto verifiedPinPane = tree ? tree->property("verifiedPinPane") : QVariant{};
        const bool verifiedPinTarget = verifiedPinPane.isValid() &&
                                       (selectedKind == "table" || selectedKind == "view") &&
                                       tree->property("verifiedPinParentId").toString() ==
                                           object.data(NavigatorModel::ObjectIdRole).toString();
        const auto pane = verifiedPinTarget              ? verifiedPinPane.toInt()
                          : selectedKind == "column"     ? 0
                          : selectedKind == "index"      ? 1
                          : selectedKind.contains("key") ? 2
                          : selectedKind == "ddl"        ? 3
                          : selectedKind == "data"       ? 5
                          : selectedKind == "table"      ? 5
                                                         : -1;
        openObjectTab(*browsingConnection_, object.data(NavigatorModel::ObjectIdRole).toString(),
                      object.data(NavigatorModel::QualifiedNameRole).toString(), kind,
                      object.data(NavigatorModel::PropertiesRole).toList(), pane);
    }
}
} // namespace choscordb
