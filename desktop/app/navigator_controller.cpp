#include "app/navigator_controller.h"
#include "app/quick_search_match.h"
#include "bridge/engine_adapter.h"
#include "bridge/rust_text.h"
#include "bridge/template_service.h"
#include "design_system/menu/menu.h"
#include "models/navigator_model.h"
#include <QApplication>
#include <QClipboard>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QVariantMap>
#include <limits>
namespace choscordb {
namespace {
constexpr auto text = &bridge_detail::fromRust;
constexpr auto relationSubtype = &NavigatorModel::relationSubtype;
class SelectedConnectionProxy final : public QSortFilterProxyModel {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setVisible(const QList<quint64>& orderedIds) {
        if (ordered_ == orderedIds)
            return;
        ordered_ = orderedIds;
        refreshFilter();
        sort(0);
    }
    const QList<quint64>& visible() const { return ordered_; }
    void refreshVisibility() { refreshFilter(); }

  protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        const auto index = sourceModel()->index(row, 0, parent);
        if (!static_cast<const NavigatorModel*>(sourceModel())->isBrowsable(index) ||
            !showsSidebarChild(index))
            return false;
        if (!ordered_.contains(index.data(NavigatorModel::ConnectionRole).toULongLong()))
            return false;
        if (!parent.isValid() && index.data(NavigatorModel::KindRole).toString() == "loading")
            return true;
        return QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        if (!left.parent().isValid() && !right.parent().isValid()) {
            const auto leftOrder =
                ordered_.indexOf(left.data(NavigatorModel::ConnectionRole).toULongLong());
            const auto rightOrder =
                ordered_.indexOf(right.data(NavigatorModel::ConnectionRole).toULongLong());
            return leftOrder < rightOrder;
        }
        return left.row() < right.row();
    }

  private:
    void refreshFilter() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
    }
    QList<quint64> ordered_;
};
} // namespace
NavigatorController::NavigatorController(EngineAdapter* engine, QTreeView* tree, QLineEdit* filter)
    : QObject(tree), model_(new NavigatorModel(this)), tree_(tree),
      proxy_(new SelectedConnectionProxy(this)), filter_(filter) {
    auto* proxy = proxy_;
    proxy->setSourceModel(model_);
    proxy->setRecursiveFilteringEnabled(true);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    tree->setModel(proxy);
    // Filter and search once typing pauses; clearing applies at once so reveals can map rows.
    filterTimer_ = new QTimer(this);
    filterTimer_->setSingleShot(true);
    connect(filterTimer_, &QTimer::timeout, this, [this] {
        proxy_->setFilterFixedString(filter_->text());
        advanceSearch(searchGeneration_);
    });
    connect(filter, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.trimmed().isEmpty()) {
            filterTimer_->stop();
            proxy_->setFilterFixedString(text);
        } else {
            filterTimer_->start(150);
        }
        restartSearch(false);
    });
    const auto restartLater = coalescedCall(this, 200, [this] {
        const auto generation = searchGeneration_;
        QTimer::singleShot(0, this, [this, generation] { advanceSearch(generation); });
    });
    connect(model_, &NavigatorModel::completionChanged, this,
            [this, restartLater](quint64 connection) {
                if (filter_->text().trimmed().isEmpty() ||
                    !static_cast<SelectedConnectionProxy*>(proxy_)->visible().contains(connection))
                    return;
                if (searchPending_ && connection == searchPendingConnection_ &&
                    model_->pendingRequestToken(searchPendingIndex_) != searchPendingToken_) {
                    searchRequestFinished(true, false);
                    return;
                }
                if (searchPending_)
                    return;
                // Other metadata changes restart the search, at most once per burst of pages.
                restartLater();
            });
    const auto rescanQuickObjects = [this] {
        if (quickObjectQuery_.isEmpty() || !quickObjectConnectionValid_)
            return;
        // A refresh may have removed a previously published object. Invalidate
        // rows before the next event-loop turn, when the fresh scan runs.
        quickObjectResults_.clear();
        quickObjectIncomplete_ = true;
        quickObjectStatus_ = tr("Searching objects…");
        emit quickObjectSearchChanged();
        const auto generation = quickObjectGeneration_;
        QTimer::singleShot(0, this, [this, generation] { advanceQuickObjectSearch(generation); });
    };
    connect(model_, &NavigatorModel::completionChanged, this,
            [this, rescanQuickObjects,
             rescanLater = coalescedCall(this, 200, rescanQuickObjects)](quint64 connection) {
                if (connection != quickObjectConnection_)
                    return;
                // The scan's own request continues at once; other changes rescan per burst.
                if (std::exchange(quickObjectAwaiting_, false))
                    rescanQuickObjects();
                else
                    rescanLater();
            });
    connect(model_, &NavigatorModel::childrenRequested, engine, &EngineAdapter::loadMetadata);
    connect(model_, &NavigatorModel::childrenPageRequested, engine,
            &EngineAdapter::loadMetadataPage);
    connect(tree, &QTreeView::activated, this, [this](const QModelIndex& index) {
        const auto source = proxy_->mapToSource(index);
        if (source.data(NavigatorModel::KindRole).toString() == "load_more")
            model_->requestNextPage(source);
    });
    connect(engine, &EngineAdapter::metadataSubmissionFailed, this,
            [this](quint64 connection, const QString& parent, quint64 token, const QString& error) {
                const bool wasPending = matchesSearchRequest(connection, parent, token);
                const bool accepted = model_->failChildren(connection, parent, token, error);
                searchRequestFinished(accepted && wasPending, true, error);
            });
    connect(
        engine, &EngineAdapter::eventReady, this,
        [this](const BridgeEvent& e) {
            const auto kind = text(e.kind);
            if (kind == "disconnected")
                model_->removeConnection(e.id);
            else if (kind == "metadata") {
                const bool wasPending = matchesSearchRequest(e.id, text(e.parent), e.request_token);
                std::vector<NavigatorObject> objects;
                objects.reserve(e.objects.size());
                for (const auto& object : e.objects) {
                    QVariantList properties;
                    for (const auto& property : object.properties)
                        properties.append(QVariantMap{{"name", text(property.name)},
                                                      {"value", text(property.value)},
                                                      {"availability", text(property.availability)},
                                                      {"reason", text(property.reason)}});
                    const auto objectKind = text(object.kind);
                    NavigatorObject entry{
                        text(object.id), text(object.name),   text(object.qualified_name),
                        objectKind,      object.has_children, std::move(properties)};
                    if (object.has_column && objectKind == QLatin1String("column"))
                        entry.databaseType = text(object.column.database_type);
                    objects.push_back(std::move(entry));
                }
                const bool accepted = model_->applyChildrenPage(
                    e.id, text(e.parent), e.request_token, std::move(objects), e.metadata_offset,
                    e.has_more_metadata, e.next_metadata_offset);
                searchRequestFinished(accepted && wasPending, false);
            } else if (kind == "metadata_failed") {
                const bool wasPending = matchesSearchRequest(e.id, text(e.parent), e.request_token);
                const bool accepted =
                    model_->failChildren(e.id, text(e.parent), e.request_token, text(e.error));
                searchRequestFinished(accepted && wasPending, true, text(e.error));
            }
        },
        Qt::DirectConnection);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeView::customContextMenuRequested, this,
            [this, tree, proxy](const QPoint& point) {
                const auto index = proxy->mapToSource(tree->indexAt(point));
                if (!index.isValid())
                    return;
                QMenu menu(tree);
                populateContextMenu(&menu, index);
                design::execContextMenu(menu, tree->viewport()->mapToGlobal(point));
            });
}
void NavigatorController::refreshCurrent() {
    const auto source = proxy_->mapToSource(tree_->currentIndex());
    if (source.isValid())
        model_->refresh(source);
}
void NavigatorController::disconnectCurrent() {
    const auto source = proxy_->mapToSource(tree_->currentIndex());
    if (source.isValid() && source.data(NavigatorModel::KindRole).toString() == "connection")
        emit disconnectRequested(source.data(NavigatorModel::ConnectionRole).toULongLong());
}
void NavigatorController::setDriverResolver(std::function<QString(quint64)> resolver) {
    driverResolver_ = std::move(resolver);
    model_->setDriverResolver(driverResolver_);
    static_cast<SelectedConnectionProxy*>(proxy_)->refreshVisibility();
}
void NavigatorController::setShowSystemSchemas(bool show) {
    if (model_->showSystemSchemas() == show)
        return;
    const QPersistentModelIndex selected(proxy_->mapToSource(tree_->currentIndex()));
    model_->setShowSystemSchemas(show);
    static_cast<SelectedConnectionProxy*>(proxy_)->refreshVisibility();
    if (selected.isValid() && !model_->isBrowsable(selected)) {
        auto ancestor = selected.parent();
        while (ancestor.isValid() && !model_->isBrowsable(ancestor))
            ancestor = ancestor.parent();
        const auto visible = proxy_->mapFromSource(ancestor);
        if (visible.isValid())
            tree_->selectionModel()->setCurrentIndex(visible, QItemSelectionModel::ClearAndSelect |
                                                                  QItemSelectionModel::Rows);
        else
            tree_->selectionModel()->clearCurrentIndex();
    }
    restartSearch();
    emit browsingVisibilityChanged();
}
void NavigatorController::setPinStateResolver(
    std::function<std::optional<bool>(const QModelIndex&)> resolver) {
    pinStateResolver_ = std::move(resolver);
}
bool NavigatorController::resolveObject(
    quint64 connection, const QStringList& ancestryIds, const QString& objectId,
    const QString& kind, const QString& qualifiedName, const QString& subtype,
    std::function<void(RevealResult, const QString&, const QModelIndex&)> finished,
    std::function<bool()> stillCurrent, bool refreshFinalParent) {
    return lookupObject(connection, ancestryIds, objectId, kind, qualifiedName, subtype,
                        std::move(finished), std::move(stillCurrent), false, refreshFinalParent);
}
bool NavigatorController::revealObject(quint64 connection, const QStringList& ancestryIds,
                                       const QString& objectId, const QString& kind,
                                       const QString& qualifiedName, const QString& subtype,
                                       std::function<void(RevealResult, const QString&)> finished,
                                       std::function<bool()> stillCurrent) {
    return lookupObject(
        connection, ancestryIds, objectId, kind, qualifiedName, subtype,
        [this, finished = std::move(finished)](RevealResult result, const QString& reason,
                                               const QModelIndex& source) {
            if (result != RevealResult::Found) {
                if (finished)
                    finished(result, reason);
                return;
            }
            const auto parent = source.parent();
            const auto parentKind = parent.data(NavigatorModel::KindRole).toString();
            const auto sourceKind = source.data(NavigatorModel::KindRole).toString();
            const bool hiddenTableDetail =
                (parentKind == QLatin1String("table") || parentKind == QLatin1String("view")) &&
                (sourceKind == QLatin1String("index") || sourceKind.contains("key"));
            const auto target = hiddenTableDetail ? parent : source;
            filter_->clear();
            for (auto ancestor = target.parent(); ancestor.isValid();
                 ancestor = ancestor.parent()) {
                const auto visible = proxy_->mapFromSource(ancestor);
                if (visible.isValid())
                    tree_->expand(visible);
            }
            const auto visible = proxy_->mapFromSource(target);
            if (!visible.isValid()) {
                if (finished)
                    finished(RevealResult::Retry,
                             tr("The object could not be shown. Activate the pin to retry."));
                return;
            }
            if (hiddenTableDetail) {
                tree_->setProperty("verifiedPinPane", sourceKind == QLatin1String("index") ? 1 : 2);
                tree_->setProperty("verifiedPinParentId",
                                   parent.data(NavigatorModel::ObjectIdRole));
            }
            tree_->setCurrentIndex(visible);
            if (hiddenTableDetail) {
                tree_->setProperty("verifiedPinPane", {});
                tree_->setProperty("verifiedPinParentId", {});
            }
            if (tree_->currentIndex() != visible) {
                if (finished)
                    finished(RevealResult::Retry, tr("Finish active database work before opening "
                                                     "this pin. Activate it to retry."));
                return;
            }
            tree_->scrollTo(visible);
            if (finished)
                finished(RevealResult::Found, {});
        },
        std::move(stillCurrent), true, true);
}
bool NavigatorController::lookupObject(
    quint64 connection, const QStringList& ancestryIds, const QString& objectId,
    const QString& kind, const QString& qualifiedName, const QString& subtype,
    std::function<void(RevealResult, const QString&, const QModelIndex&)> finished,
    std::function<bool()> stillCurrent, bool requireVisibleConnection, bool refreshFinalParent) {
    QModelIndex root;
    for (int row = 0; row < model_->rowCount(); ++row) {
        const auto candidate = model_->index(row, 0);
        if (candidate.data(NavigatorModel::ConnectionRole).toULongLong() == connection &&
            candidate.data(NavigatorModel::KindRole).toString() == QStringLiteral("connection")) {
            root = candidate;
            break;
        }
    }
    if (!root.isValid() ||
        (requireVisibleConnection &&
         !static_cast<SelectedConnectionProxy*>(proxy_)->visible().contains(connection)))
        return false;
    struct State {
        QPersistentModelIndex parent;
        QStringList route;
        QString objectId, kind, qualifiedName, subtype;
        quint64 connection = 0;
        int depth = 0, requests = 0;
        bool finalRefreshStarted = false;
        bool requireVisibleConnection = false;
        bool refreshFinalParent = true;
        QSet<QString> requestedParents;
        QPointer<QObject> task;
        std::function<void(RevealResult, const QString&, const QModelIndex&)> finished;
        std::function<bool()> stillCurrent;
        std::function<void()> step;
    };
    auto state = std::make_shared<State>();
    state->parent = root;
    state->route = ancestryIds;
    state->objectId = objectId;
    state->kind = kind;
    state->qualifiedName = qualifiedName;
    state->subtype = subtype;
    state->connection = connection;
    state->requireVisibleConnection = requireVisibleConnection;
    state->refreshFinalParent = refreshFinalParent;
    state->finished = std::move(finished);
    state->stillCurrent = std::move(stillCurrent);
    state->task = new QObject(this);
    // Restarting an active zero-delay timer keeps at most one queued step per lookup.
    auto* stepTimer = new QTimer(state->task);
    stepTimer->setSingleShot(true);
    connect(stepTimer, &QTimer::timeout, state->task, [state] { state->step(); });
    const auto weak = std::weak_ptr<State>(state);
    state->step = [this, weak, stepTimer] {
        const auto state = weak.lock();
        if (!state || !state->task)
            return;
        const auto finish = [state](RevealResult result, const QString& reason,
                                    const QModelIndex& source = QModelIndex()) {
            auto finished = std::move(state->finished);
            const auto task = state->task;
            state->task = nullptr;
            if (finished)
                finished(result, reason, source);
            if (task)
                task->deleteLater();
        };
        if (state->stillCurrent && !state->stillCurrent()) {
            finish(RevealResult::Retry, {});
            return;
        }
        if (!state->parent.isValid() ||
            (state->requireVisibleConnection &&
             !static_cast<SelectedConnectionProxy*>(proxy_)->visible().contains(
                 state->connection))) {
            finish(RevealResult::Retry,
                   tr("The connection is no longer visible. Activate the pin to retry."));
            return;
        }
        const auto parent = QModelIndex(state->parent);
        if (state->refreshFinalParent && state->depth == state->route.size() &&
            !state->finalRefreshStarted) {
            state->finalRefreshStarted = true;
            const auto parentId = parent.data(NavigatorModel::ObjectIdRole).toString();
            if (!model_->canFetchMore(parent) && !state->requestedParents.contains(parentId)) {
                if (++state->requests > 32) {
                    finish(RevealResult::Retry,
                           tr("The metadata lookup limit was reached. Activate the pin to retry."));
                    return;
                }
                state->requestedParents.insert(parentId);
                model_->refresh(parent);
                return;
            }
        }
        const auto error = parent.data(NavigatorModel::ErrorRole).toString();
        if (!error.isEmpty()) {
            const auto parentId = parent.data(NavigatorModel::ObjectIdRole).toString();
            if (!state->requestedParents.contains(parentId) && state->requests < 32) {
                state->requestedParents.insert(parentId);
                ++state->requests;
                model_->refresh(parent);
                return;
            }
            finish(RevealResult::Retry,
                   tr("Metadata could not load: %1. Activate the pin to retry.").arg(error));
            return;
        }
        const auto expected =
            state->depth < state->route.size() ? state->route.at(state->depth) : state->objectId;
        for (int row = 0; row < model_->rowCount(parent); ++row) {
            const auto child = model_->index(row, 0, parent);
            if (child.data(NavigatorModel::ObjectIdRole).toString() != expected ||
                child.data(NavigatorModel::KindRole).toString() == QStringLiteral("load_more"))
                continue;
            if (state->depth < state->route.size()) {
                state->parent = child;
                ++state->depth;
                stepTimer->start();
                return;
            }
            if (child.data(NavigatorModel::KindRole).toString() != state->kind ||
                child.data(NavigatorModel::QualifiedNameRole).toString() != state->qualifiedName ||
                relationSubtype(child.data(NavigatorModel::PropertiesRole).toList()) !=
                    state->subtype) {
                finish(RevealResult::Unavailable,
                       tr("The pinned object no longer matches its saved identity."));
                return;
            }
            finish(RevealResult::Found, {}, child);
            return;
        }
        if (parent.data(NavigatorModel::ChildrenLoadedRole).toBool() ||
            !model_->hasChildren(parent)) {
            finish(RevealResult::Unavailable,
                   tr("The pinned object is no longer in its saved location."));
            return;
        }
        if (++state->requests > 32) {
            finish(RevealResult::Retry,
                   tr("The metadata lookup limit was reached. Activate the pin to retry."));
            return;
        }
        if (model_->canFetchMore(parent)) {
            state->requestedParents.insert(parent.data(NavigatorModel::ObjectIdRole).toString());
            model_->fetchMore(parent);
        } else if (parent.data(NavigatorModel::HasMoreRole).toBool()) {
            state->requestedParents.insert(parent.data(NavigatorModel::ObjectIdRole).toString());
            model_->requestNextPage(parent);
        } else
            --state->requests; // The current page is already loading.
    };
    connect(model_, &NavigatorModel::completionChanged, state->task,
            [state, stepTimer](quint64 changedConnection) {
                if (state->connection == changedConnection && state->task)
                    stepTimer->start();
            });
    stepTimer->start();
    return true;
}
void NavigatorController::populateContextMenu(QMenu* menu, const QModelIndex& sourceIndex) {
    if (!menu || !sourceIndex.isValid() || sourceIndex.model() != model_)
        return;
    const QPersistentModelIndex index(sourceIndex);
    if (index.data(NavigatorModel::KindRole).toString() == "load_more" ||
        index.data(NavigatorModel::HasMoreRole).toBool()) {
        auto* more = menu->addAction(tr("Load more objects"));
        more->setObjectName("loadMoreMetadata");
        connect(more, &QAction::triggered, this, [this, index] {
            if (index.isValid())
                model_->requestNextPage(index);
        });
        if (index.data(NavigatorModel::KindRole).toString() == "load_more")
            return;
    }
    if (index.data(NavigatorModel::KindRole).toString() == "connection") {
        auto* disconnect = menu->addAction(tr("Disconnect"));
        disconnect->setObjectName("disconnectSession");
        connect(disconnect, &QAction::triggered, this, [this, index] {
            if (index.isValid() && index.data(NavigatorModel::KindRole).toString() == "connection")
                emit disconnectRequested(index.data(NavigatorModel::ConnectionRole).toULongLong());
        });
        menu->addSeparator();
    }
    const auto pinKind = index.data(NavigatorModel::KindRole).toString();
    static const QSet<QString> pinKinds = {
        QStringLiteral("schema"),   QStringLiteral("table"),      QStringLiteral("view"),
        QStringLiteral("index"),    QStringLiteral("sequence"),   QStringLiteral("function"),
        QStringLiteral("column"),   QStringLiteral("primarykey"), QStringLiteral("foreignkey"),
        QStringLiteral("uniquekey")};
    if (pinKinds.contains(pinKind) &&
        !index.data(NavigatorModel::ObjectIdRole).toString().isEmpty()) {
        const auto state = pinStateResolver_ ? pinStateResolver_(index) : std::nullopt;
        auto* pin = menu->addAction(state.value_or(false) ? tr("Unpin") : tr("Pin"));
        pin->setObjectName(state.value_or(false) ? "unpinObject" : "pinObject");
        pin->setEnabled(state.has_value());
        if (!state) {
            const auto reason = tr("Save this connection before pinning its objects.");
            pin->setToolTip(reason);
            pin->setStatusTip(reason);
            menu->setToolTipsVisible(true);
        }
        const auto connection = index.data(NavigatorModel::ConnectionRole).toULongLong();
        const auto objectId = index.data(NavigatorModel::ObjectIdRole).toString();
        connect(pin, &QAction::triggered, this,
                [this, index, connection, objectId, pinKind, unpin = state.value_or(false)] {
                    if (!index.isValid() || index.model() != model_ ||
                        index.data(NavigatorModel::ConnectionRole).toULongLong() != connection ||
                        index.data(NavigatorModel::ObjectIdRole).toString() != objectId ||
                        index.data(NavigatorModel::KindRole).toString() != pinKind ||
                        !pinStateResolver_ ||
                        pinStateResolver_(index) != std::optional<bool>(unpin))
                        return;
                    emit pinRequested(index, unpin);
                });
        menu->addSeparator();
    }
    auto* refresh = menu->addAction(tr("Refresh"));
    connect(refresh, &QAction::triggered, this, [this, index] {
        if (index.isValid())
            model_->refresh(index);
    });
    auto* copy = menu->addAction(tr("Copy qualified name"));
    copy->setEnabled(!index.data(NavigatorModel::QualifiedNameRole).toString().isEmpty());
    connect(copy, &QAction::triggered, this, [index] {
        if (index.isValid())
            QApplication::clipboard()->setText(
                index.data(NavigatorModel::QualifiedNameRole).toString());
    });
    const auto objectKind = index.data(NavigatorModel::KindRole).toString();
    auto* ddl = menu->addAction(tr("Show DDL"));
    ddl->setEnabled(objectKind == "table" || objectKind == "view" || objectKind == "index" ||
                    objectKind == "sequence" || objectKind == "function");
    connect(ddl, &QAction::triggered, this, [this, index] {
        if (index.isValid())
            emit ddlRequested(index.data(NavigatorModel::ConnectionRole).toULongLong(),
                              index.data(NavigatorModel::ObjectIdRole).toString(),
                              index.data(Qt::DisplayRole).toString(),
                              index.data(NavigatorModel::KindRole).toString(),
                              index.data(NavigatorModel::PropertiesRole).toList());
    });
    if (objectKind != "table" && objectKind != "view")
        return;
    const auto connection = index.data(NavigatorModel::ConnectionRole).toULongLong();
    const auto objectId = index.data(NavigatorModel::ObjectIdRole).toString();
    const auto shortName = index.data(Qt::DisplayRole).toString();
    const auto qualifiedName = index.data(NavigatorModel::QualifiedNameRole).toString();
    const auto parentObjectId = index.parent().data(NavigatorModel::ObjectIdRole).toString();
    const auto subtype = relationSubtype(index.data(NavigatorModel::PropertiesRole).toList());
    const auto driver = driverResolver_ ? driverResolver_(connection).toLower() : QString{};
    const bool supportedDriver = driver == "sqlite" || driver == "postgres" || driver == "mysql";
    const bool sqliteView = driver == "sqlite" && objectKind == "view";
    menu->addSeparator();
    auto* drop = menu->addAction(tr("Drop"));
    drop->setObjectName("dropObject");
    drop->setEnabled(supportedDriver);
    auto* rename = menu->addAction(sqliteView ? tr("Rename (SQLite does not support view rename)")
                                              : tr("Rename"));
    rename->setObjectName("renameObject");
    rename->setEnabled(supportedDriver && !sqliteView);
    if (sqliteView) {
        const auto reason = tr("SQLite does not support renaming a view directly.");
        rename->setToolTip(reason);
        rename->setStatusTip(reason);
        menu->setToolTipsVisible(true);
    }
    const auto dispatch = [this, index, connection, objectId, shortName, objectKind, parentObjectId,
                           qualifiedName, subtype](const QString& action) {
        if (!index.isValid() || index.model() != model_ ||
            index.data(NavigatorModel::ConnectionRole).toULongLong() != connection ||
            index.data(NavigatorModel::ObjectIdRole).toString() != objectId ||
            index.data(NavigatorModel::KindRole).toString() != objectKind ||
            index.data(Qt::DisplayRole).toString() != shortName ||
            index.data(NavigatorModel::QualifiedNameRole).toString() != qualifiedName ||
            index.parent().data(NavigatorModel::ObjectIdRole).toString() != parentObjectId ||
            relationSubtype(index.data(NavigatorModel::PropertiesRole).toList()) != subtype)
            return;
        emit objectActionRequested(action, connection, objectId, shortName, objectKind,
                                   parentObjectId, qualifiedName, subtype);
    };
    connect(drop, &QAction::triggered, this, [dispatch] { dispatch(QStringLiteral("drop")); });
    connect(rename, &QAction::triggered, this, [dispatch] { dispatch(QStringLiteral("rename")); });
    menu->addSeparator();
    auto* generate = menu->addMenu(tr("Generate SQL"));
    const bool loaded = index.data(NavigatorModel::ChildrenLoadedRole).toBool();
    bool hasColumn = false;
    // The menu need only find one column to enable UPDATE; cap even this scan.
    const auto maximum = SqlTemplateService::limits();
    const auto scanLimit = maximum.maxColumns * 4 + 64;
    for (int row = 0; loaded && row < model_->rowCount(index) && quint64(row) < scanLimit; ++row)
        if (model_->index(row, 0, index).data(NavigatorModel::KindRole).toString() == "column") {
            hasColumn = true;
            break;
        }
    for (const auto& kind :
         {QString("select"), QString("insert"), QString("update"), QString("delete")}) {
        auto* action = generate->addAction(kind.toUpper());
        action->setObjectName("generate_" + kind);
        action->setEnabled(kind == "select" || kind == "delete" ||
                           (loaded && (kind == "insert" || hasColumn)));
        connect(action, &QAction::triggered, this, [this, index, kind] {
            if (!index.isValid() || index.model() != model_) {
                emit generationFailed(tr("The selected object is no longer available."));
                return;
            }
            const auto objectKind = index.data(NavigatorModel::KindRole).toString();
            if (objectKind != "table" && objectKind != "view") {
                emit generationFailed(tr("Select a table or view to generate SQL."));
                return;
            }
            QStringList columns;
            if (kind == "insert" || kind == "update") {
                if (!index.data(NavigatorModel::ChildrenLoadedRole).toBool()) {
                    emit generationFailed(
                        tr("Expand this object to load columns before generating SQL."));
                    return;
                }
                const auto limits = SqlTemplateService::limits();
                quint64 characters = 0;
                if (quint64(model_->rowCount(index)) > limits.maxColumns * 4 + 64) {
                    emit generationFailed(tr("Too many metadata objects to generate SQL."));
                    return;
                }
                for (int row = 0; row < model_->rowCount(index); ++row) {
                    const auto child = model_->index(row, 0, index);
                    if (child.data(NavigatorModel::KindRole).toString() != "column")
                        continue;
                    const auto name = child.data(Qt::DisplayRole).toString();
                    if (quint64(columns.size()) == limits.maxColumns ||
                        quint64(name.size()) > limits.maxBytes - characters) {
                        emit generationFailed(
                            tr("Column metadata exceeds the SQL template limits."));
                        return;
                    }
                    characters += quint64(name.size());
                    QString owned(name.constData(), name.size());
                    owned.squeeze();
                    columns.append(std::move(owned));
                }
            }
            const auto result = SqlTemplateService::generate(
                kind, index.data(NavigatorModel::QualifiedNameRole).toString(), columns);
            if (!result.valid) {
                emit generationFailed(result.error);
                return;
            }
            emit sqlGenerated(index.data(NavigatorModel::ConnectionRole).toULongLong(), result.sql);
        });
    }
    if (!loaded) {
        auto* hint = generate->addAction(tr("Expand this object to load columns."));
        hint->setEnabled(false);
    }
}
void NavigatorController::addConnection(quint64 connection, const QString& label) {
    if (!model_->addConnection(connection, label))
        return;
    if (!quickObjectQuery_.isEmpty() && quickObjectConnectionValid_ &&
        connection == quickObjectConnection_) {
        const auto generation = quickObjectGeneration_;
        QTimer::singleShot(0, this, [this, generation] { advanceQuickObjectSearch(generation); });
    }
    if (!filter_->text().trimmed().isEmpty() &&
        static_cast<SelectedConnectionProxy*>(proxy_)->visible().contains(connection))
        restartSearch();
}
void NavigatorController::renameConnection(quint64 connection, const QString& label) {
    model_->renameConnection(connection, label);
}
void NavigatorController::setVisibleConnections(const QList<quint64>& orderedIds) {
    QList<quint64> uniqueIds;
    for (const auto id : orderedIds)
        if (!uniqueIds.contains(id))
            uniqueIds.append(id);
    auto* selected = static_cast<SelectedConnectionProxy*>(proxy_);
    if (selected->visible() == uniqueIds)
        return;
    selected->setVisible(uniqueIds);
    if (!quickObjectQuery_.isEmpty() || !quickObjectResults_.isEmpty())
        cancelQuickObjectSearch();
    emit selectedConnectionsChanged();
    restartSearch();
}
void NavigatorController::setSelectedConnection(quint64 connection) {
    removePendingConnection(std::numeric_limits<quint64>::max());
    setVisibleConnections({connection});
}
void NavigatorController::clearSelectedConnection() {
    removePendingConnection(std::numeric_limits<quint64>::max());
    setVisibleConnections({});
}
void NavigatorController::setPendingConnection(quint64 pendingId, const QString& label) {
    model_->addPendingConnection(pendingId, label);
}
void NavigatorController::removePendingConnection(quint64 pendingId) {
    model_->removeConnection(pendingId);
}
void NavigatorController::setPendingConnection(const QString& label) {
    constexpr auto pendingId = std::numeric_limits<quint64>::max();
    removePendingConnection(pendingId);
    setPendingConnection(pendingId, label);
    setVisibleConnections({pendingId});
}
quint64 NavigatorController::selectedConnection() const {
    const auto& visible = static_cast<SelectedConnectionProxy*>(proxy_)->visible();
    return visible.isEmpty() ? 0 : visible.first();
}
bool NavigatorController::hasSelectedConnection() const {
    return !static_cast<SelectedConnectionProxy*>(proxy_)->visible().isEmpty();
}
bool NavigatorController::isVisibleConnection(quint64 connection) const {
    return static_cast<SelectedConnectionProxy*>(proxy_)->visible().contains(connection);
}
void NavigatorController::startQuickObjectSearch(const QString& query,
                                                 std::optional<quint64> connection, int delayMs) {
    quickObjectQuery_ = query.trimmed();
    quickObjectResults_.clear();
    quickObjectRequests_ = 0;
    quickObjectAwaiting_ = false;
    quickObjectIncomplete_ = !quickObjectQuery_.isEmpty();
    const auto& visible = static_cast<SelectedConnectionProxy*>(proxy_)->visible();
    quickObjectConnectionValid_ = connection ? visible.contains(*connection) : !visible.isEmpty();
    quickObjectConnection_ = connection.value_or(visible.isEmpty() ? 0 : visible.first());
    quickObjectStatus_ = quickObjectQuery_.isEmpty() ? QString{}
                         : !quickObjectConnectionValid_
                             ? tr("Select an available connection to search objects.")
                             : tr("Searching objects…");
    ++quickObjectGeneration_;
    emit quickObjectSearchChanged();
    if (!quickObjectQuery_.isEmpty() && quickObjectConnectionValid_) {
        const auto generation = quickObjectGeneration_;
        QTimer::singleShot(std::max(0, delayMs), this,
                           [this, generation] { advanceQuickObjectSearch(generation); });
    }
}
void NavigatorController::cancelQuickObjectSearch() {
    quickObjectQuery_.clear();
    quickObjectAwaiting_ = false;
    quickObjectResults_.clear();
    quickObjectStatus_.clear();
    quickObjectIncomplete_ = false;
    quickObjectRequests_ = 0;
    quickObjectConnectionValid_ = false;
    ++quickObjectGeneration_;
    emit quickObjectSearchChanged();
}
void NavigatorController::advanceQuickObjectSearch(quint64 generation) {
    if (generation != quickObjectGeneration_ || quickObjectQuery_.isEmpty() ||
        !quickObjectConnectionValid_)
        return;
    constexpr int visitLimit = 10000;
    constexpr int requestLimit = 96;
    constexpr int resultLimit = 100;
    const auto& visible = static_cast<SelectedConnectionProxy*>(proxy_)->visible();
    if (!visible.contains(quickObjectConnection_))
        return;
    const QuickSearchNeedle needle(quickObjectQuery_);
    const auto matches = [&needle](const QString& candidate) {
        return needle.score(candidate).has_value();
    };
    std::vector<QModelIndex> stack;
    for (int row = model_->rowCount() - 1; row >= 0; --row) {
        const auto root = model_->index(row, 0);
        if (root.data(NavigatorModel::ConnectionRole).toULongLong() == quickObjectConnection_)
            stack.push_back(root);
    }
    QList<QuickObjectResult> results;
    QString error;
    bool loading = false;
    bool limited = false;
    int visited = 0;
    while (!stack.empty()) {
        if (++visited > visitLimit) {
            limited = true;
            break;
        }
        const auto current = stack.back();
        stack.pop_back();
        if (!model_->isBrowsable(current))
            continue;
        const auto kind = current.data(NavigatorModel::KindRole).toString();
        if (kind == QStringLiteral("loading")) {
            loading = true;
            continue;
        }
        if (kind == QStringLiteral("load_more") || kind == QStringLiteral("error"))
            continue;
        const auto nodeError = current.data(NavigatorModel::ErrorRole).toString();
        if (error.isEmpty() && !nodeError.isEmpty())
            error = nodeError;
        if (kind != QStringLiteral("connection") &&
            (matches(current.data(Qt::DisplayRole).toString()) ||
             matches(current.data(NavigatorModel::QualifiedNameRole).toString()))) {
            auto target = current;
            QString targetKind = kind;
            while (target.isValid() && targetKind != QStringLiteral("table") &&
                   targetKind != QStringLiteral("view") && targetKind != QStringLiteral("index") &&
                   targetKind != QStringLiteral("sequence") &&
                   targetKind != QStringLiteral("function")) {
                target = target.parent();
                targetKind = target.data(NavigatorModel::KindRole).toString();
            }
            if (target.isValid()) {
                if (results.size() == resultLimit) {
                    limited = true;
                    break;
                }
                QStringList ancestors;
                for (auto parent = current.parent(); parent.isValid(); parent = parent.parent())
                    ancestors.prepend(parent.data(Qt::DisplayRole).toString());
                QuickObjectResult result;
                result.connection = current.data(NavigatorModel::ConnectionRole).toULongLong();
                result.objectId = current.data(NavigatorModel::ObjectIdRole).toString();
                result.name = current.data(Qt::DisplayRole).toString();
                result.qualifiedName = current.data(NavigatorModel::QualifiedNameRole).toString();
                result.kind = kind;
                result.parentObjectId =
                    current.parent().data(NavigatorModel::ObjectIdRole).toString();
                result.context = ancestors.join(QStringLiteral(" / "));
                result.properties = current.data(NavigatorModel::PropertiesRole).toList();
                result.targetObjectId = target.data(NavigatorModel::ObjectIdRole).toString();
                result.targetQualifiedName =
                    target.data(NavigatorModel::QualifiedNameRole).toString();
                result.targetKind = targetKind;
                result.targetParentObjectId =
                    target.parent().data(NavigatorModel::ObjectIdRole).toString();
                result.targetProperties = target.data(NavigatorModel::PropertiesRole).toList();
                if (kind == QStringLiteral("column"))
                    result.targetPane = 0;
                else if (kind == QStringLiteral("index"))
                    result.targetPane = 1;
                else if (kind.contains(QStringLiteral("key")))
                    result.targetPane = 2;
                else if (kind == QStringLiteral("ddl"))
                    result.targetPane = 3;
                else if (kind == QStringLiteral("data") || kind == QStringLiteral("table"))
                    result.targetPane = 5;
                results.append(std::move(result));
            }
        }
        if (model_->canFetchMore(current) || current.data(NavigatorModel::HasMoreRole).toBool()) {
            if (quickObjectRequests_ >= requestLimit) {
                limited = true;
                break;
            }
            const QPersistentModelIndex requestIndex(current);
            const bool hasMore = current.data(NavigatorModel::HasMoreRole).toBool();
            ++quickObjectRequests_;
            quickObjectAwaiting_ = true;
            quickObjectResults_ = std::move(results);
            quickObjectIncomplete_ = true;
            quickObjectStatus_ = tr("Searching objects…");
            emit quickObjectSearchChanged();
            if (generation != quickObjectGeneration_ || !requestIndex.isValid())
                return;
            if (hasMore)
                model_->requestNextPage(requestIndex);
            else
                model_->fetchMore(requestIndex);
            return;
        }
        for (int row = model_->rowCount(current) - 1; row >= 0; --row)
            stack.push_back(model_->index(row, 0, current));
    }
    quickObjectResults_ = std::move(results);
    quickObjectIncomplete_ = limited || loading || !error.isEmpty();
    if (limited)
        quickObjectStatus_ = tr("Object search incomplete: limit reached. Refine the query.");
    else if (!error.isEmpty())
        quickObjectStatus_ = tr("Object search incomplete: %1. Refine or retry.").arg(error);
    else if (loading)
        quickObjectStatus_ = tr("Searching objects…");
    else
        quickObjectStatus_.clear();
    emit quickObjectSearchChanged();
}
void NavigatorController::restartSearch(bool advance) {
    ++searchGeneration_;
    searchRequests_ = 0;
    searchPending_ = false;
    searchError_.clear();
    resetSearch();
    if (filter_->text().trimmed().isEmpty()) {
        emit searchStatusChanged({});
        return;
    }
    emit searchStatusChanged(tr("Searching objects…"));
    if (advance)
        advanceSearch(searchGeneration_);
}
void NavigatorController::searchRequestFinished(bool accepted, bool failed, const QString& error) {
    if (!accepted)
        return;
    searchPending_ = false;
    if (failed && filter_->text().trimmed().isEmpty())
        return;
    if (failed)
        searchError_ = error;
    const auto generation = searchGeneration_;
    QTimer::singleShot(0, this, [this, generation] { advanceSearch(generation, true); });
}
void NavigatorController::advanceSearch(quint64 generation, bool resume) {
    if (generation != searchGeneration_ || searchPending_ || filter_->text().trimmed().isEmpty())
        return;
    const auto& visible = static_cast<SelectedConnectionProxy*>(proxy_)->visible();
    if (visible.isEmpty()) {
        emit searchStatusChanged({});
        return;
    }
    // A finished request continues the paused traversal; anything else starts over.
    if (resume && searchStateGeneration_ == generation) {
        if (searchFinished_)
            return;
    } else {
        resetSearch();
        searchStateGeneration_ = generation;
        for (int row = 0; row < model_->rowCount(); ++row) {
            const auto root = model_->index(row, 0);
            if (visible.contains(root.data(NavigatorModel::ConnectionRole).toULongLong()))
                searchStack_.push_back({root});
        }
    }
    const auto text = filter_->text();
    const auto limitReached = [this] {
        resetSearch();
        searchStateGeneration_ = searchGeneration_;
        searchFinished_ = true;
        emit searchStatusChanged(tr("Search incomplete: limit reached. Refine the text."));
    };
    while (!searchStack_.empty()) {
        const auto entry = std::move(searchStack_.back());
        searchStack_.pop_back();
        const QModelIndex current = entry.index;
        if (!current.isValid() || !model_->isBrowsable(current))
            continue;
        if (!entry.expandOnly) {
            if (!showsSidebarChild(current))
                continue;
            if (++searchVisited_ > 20000)
                return limitReached();
            const auto kind = current.data(NavigatorModel::KindRole).toString();
            if (kind == "loading" || kind == "error")
                searchIncomplete_ = true;
            if (current.data(Qt::DisplayRole).toString().contains(text, Qt::CaseInsensitive))
                searchMatches_.emplace_back(current);
            if (kind != "connection" && kind != "database" && kind != "schema" && kind != "group" &&
                kind != "table" && kind != "view")
                continue;
        }
        if (!current.data(NavigatorModel::ErrorRole).toString().isEmpty())
            searchIncomplete_ = true;
        if (model_->canFetchMore(current) || current.data(NavigatorModel::HasMoreRole).toBool()) {
            if (searchRequests_ >= 256)
                return limitReached();
            ++searchRequests_;
            searchPending_ = true;
            searchPendingConnection_ = current.data(NavigatorModel::ConnectionRole).toULongLong();
            searchPendingIndex_ = current;
            searchStack_.push_back({QPersistentModelIndex(current), true});
            if (current.data(NavigatorModel::HasMoreRole).toBool())
                model_->requestNextPage(current);
            else
                model_->fetchMore(current);
            searchPendingToken_ = model_->pendingRequestToken(current);
            return;
        }
        for (int row = model_->rowCount(current) - 1; row >= 0; --row)
            searchStack_.push_back({model_->index(row, 0, current)});
    }
    searchFinished_ = true;
    // Expand each distinct ancestor once, even when many matches share it.
    QSet<QModelIndex> ancestors;
    for (const auto& match : std::exchange(searchMatches_, {})) {
        for (auto ancestor = QModelIndex(match).parent(); ancestor.isValid();
             ancestor = ancestor.parent()) {
            if (ancestors.contains(ancestor))
                break;
            ancestors.insert(ancestor);
            const auto visibleAncestor = proxy_->mapFromSource(ancestor);
            if (visibleAncestor.isValid() && !tree_->isExpanded(visibleAncestor))
                tree_->expand(visibleAncestor);
        }
    }
    if (!searchError_.isEmpty())
        emit searchStatusChanged(
            tr("Search incomplete: %1. Refine the text or retry.").arg(searchError_));
    else if (searchIncomplete_)
        emit searchStatusChanged(tr("Search incomplete. Refine the text or retry."));
    else
        emit searchStatusChanged({});
}
bool NavigatorController::matchesSearchRequest(quint64 connection, const QString& parent,
                                               quint64 token) const {
    return searchPending_ && connection == searchPendingConnection_ &&
           searchPendingIndex_.isValid() &&
           searchPendingIndex_.data(NavigatorModel::ObjectIdRole).toString() == parent &&
           searchPendingToken_ == token;
}
} // namespace choscordb
