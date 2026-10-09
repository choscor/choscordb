#include "app/navigator_controller.h"
#include "bridge/engine_adapter.h"
#include "bridge/rust_text.h"
#include "bridge/template_service.h"
#include "bridge/text_filter.h"
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
bool showsSidebarChild(const QModelIndex& index) {
    return EngineAdapter::sidebarChildVisible(
        index.parent().data(NavigatorModel::KindRole).toString(),
        index.data(NavigatorModel::KindRole).toString());
}
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
    void setText(const QString& text) {
        filter_ = TextFilter(text);
        refreshFilter();
    }

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
        return filter_.blank() || filter_.matches(index.data(Qt::DisplayRole).toString());
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
    TextFilter filter_;
};
} // namespace
NavigatorController::NavigatorController(EngineAdapter* engine, QTreeView* tree, QLineEdit* filter)
    : QObject(tree), model_(new NavigatorModel(this)), tree_(tree),
      proxy_(new SelectedConnectionProxy(this)), filter_(filter) {
    auto* proxy = proxy_;
    proxy->setSourceModel(model_);
    proxy->setRecursiveFilteringEnabled(true);
    tree->setModel(proxy);
    // Filter and search once typing pauses; clearing applies at once so reveals can map rows.
    filterTimer_ = new QTimer(this);
    filterTimer_->setSingleShot(true);
    connect(filterTimer_, &QTimer::timeout, this, [this] {
        static_cast<SelectedConnectionProxy*>(proxy_)->setText(filter_->text());
        advanceSearch(searchGeneration_);
    });
    connect(filter, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.trimmed().isEmpty()) {
            filterTimer_->stop();
            static_cast<SelectedConnectionProxy*>(proxy_)->setText(text);
        } else {
            filterTimer_->start(150);
        }
        restartSearch(false);
    });
    const auto restartLater = coalescedCall(this, 200, [this] {
        const auto generation = searchGeneration_;
        QTimer::singleShot(0, this, [this, generation] { advanceSearch(generation); });
    });
    connect(
        model_, &NavigatorModel::completionChanged, this, [this, restartLater](quint64 connection) {
            if (filter_->text().trimmed().isEmpty() || !visibleConnections().contains(connection))
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
        quickObjectSearching_ = true;
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
                    // Rust attaches column metadata only to column rows.
                    if (object.has_column)
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
    if (source.isValid() &&
        EngineAdapter::objectKindTraits(source.data(NavigatorModel::KindRole).toString())
            .connection)
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
                !EngineAdapter::sidebarChildVisible(parentKind, sourceKind);
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
                tree_->setProperty("verifiedPinPane",
                                   EngineAdapter::objectKindTraits(sourceKind).detailPane);
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
            EngineAdapter::objectKindTraits(candidate.data(NavigatorModel::KindRole).toString())
                .connection) {
            root = candidate;
            break;
        }
    }
    if (!root.isValid() || (requireVisibleConnection && !visibleConnections().contains(connection)))
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
        if (!state->parent.isValid() || (state->requireVisibleConnection &&
                                         !visibleConnections().contains(state->connection))) {
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
    const auto connectionNode = [](const QModelIndex& node) {
        return EngineAdapter::objectKindTraits(node.data(NavigatorModel::KindRole).toString())
            .connection;
    };
    if (connectionNode(index)) {
        auto* disconnect = menu->addAction(tr("Disconnect"));
        disconnect->setObjectName("disconnectSession");
        connect(disconnect, &QAction::triggered, this, [this, index, connectionNode] {
            if (index.isValid() && connectionNode(index))
                emit disconnectRequested(index.data(NavigatorModel::ConnectionRole).toULongLong());
        });
        menu->addSeparator();
    }
    const auto pinKind = index.data(NavigatorModel::KindRole).toString();
    const auto traits = EngineAdapter::objectKindTraits(pinKind);
    if (traits.pinnable && !index.data(NavigatorModel::ObjectIdRole).toString().isEmpty()) {
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
    ddl->setEnabled(traits.ddl);
    connect(ddl, &QAction::triggered, this, [this, index] {
        if (index.isValid())
            emit ddlRequested(index.data(NavigatorModel::ConnectionRole).toULongLong(),
                              index.data(NavigatorModel::ObjectIdRole).toString(),
                              index.data(Qt::DisplayRole).toString(),
                              index.data(NavigatorModel::KindRole).toString(),
                              index.data(NavigatorModel::PropertiesRole).toList());
    });
    if (!traits.relation)
        return;
    const auto connection = index.data(NavigatorModel::ConnectionRole).toULongLong();
    const auto objectId = index.data(NavigatorModel::ObjectIdRole).toString();
    const auto shortName = index.data(Qt::DisplayRole).toString();
    const auto qualifiedName = index.data(NavigatorModel::QualifiedNameRole).toString();
    const auto parentObjectId = index.parent().data(NavigatorModel::ObjectIdRole).toString();
    const auto subtype = relationSubtype(index.data(NavigatorModel::PropertiesRole).toList());
    const auto driver = driverResolver_ ? driverResolver_(connection).toLower() : QString{};
    menu->addSeparator();
    const auto addObjectAction = [&](const QString& label, const char* name, bool rename) {
        auto* action = menu->addAction(label);
        action->setObjectName(name);
        const auto reason =
            EngineAdapter::objectActionUnavailableReason(rename, driver, objectKind, subtype);
        action->setEnabled(reason.isEmpty());
        if (!reason.isEmpty()) {
            action->setToolTip(reason);
            action->setStatusTip(reason);
            menu->setToolTipsVisible(true);
        }
        return action;
    };
    auto* drop = addObjectAction(tr("Drop"), "dropObject", false);
    auto* rename = addObjectAction(tr("Rename"), "renameObject", true);
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
    const auto columnNames = [this](const QModelIndex& relation, bool firstOnly) {
        QStringList names;
        if (!relation.data(NavigatorModel::ChildrenLoadedRole).toBool())
            return names;
        for (int row = 0; row < model_->rowCount(relation); ++row) {
            const auto child = model_->index(row, 0, relation);
            if (!EngineAdapter::objectKindTraits(child.data(NavigatorModel::KindRole).toString())
                     .column)
                continue;
            names.append(child.data(Qt::DisplayRole).toString());
            if (firstOnly)
                break;
        }
        return names;
    };
    const bool columnsLoaded = index.data(NavigatorModel::ChildrenLoadedRole).toBool();
    const bool hasColumn = !columnNames(index, true).isEmpty();
    for (const auto& kind :
         {QString("select"), QString("insert"), QString("update"), QString("delete")}) {
        auto* action = generate->addAction(kind.toUpper());
        action->setObjectName("generate_" + kind);
        const auto reason = SqlTemplateService::unavailableReason(kind, columnsLoaded, hasColumn);
        action->setEnabled(reason.isEmpty());
        if (!reason.isEmpty())
            action->setToolTip(reason);
        connect(action, &QAction::triggered, this, [this, index, kind, columnNames] {
            if (!index.isValid() || index.model() != model_) {
                emit generationFailed(tr("The selected object is no longer available."));
                return;
            }
            const auto objectKind = index.data(NavigatorModel::KindRole).toString();
            if (!EngineAdapter::objectKindTraits(objectKind).relation) {
                emit generationFailed(tr("Select a table or view to generate SQL."));
                return;
            }
            // Rust rejects templates whose columns are not loaded or exceed its limits.
            const auto result = SqlTemplateService::generate(
                kind, index.data(NavigatorModel::QualifiedNameRole).toString(),
                columnNames(index, false), index.data(NavigatorModel::ChildrenLoadedRole).toBool());
            if (!result.valid) {
                emit generationFailed(result.error);
                return;
            }
            emit sqlGenerated(index.data(NavigatorModel::ConnectionRole).toULongLong(), result.sql);
        });
    }
    if (!index.data(NavigatorModel::ChildrenLoadedRole).toBool()) {
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
    if (!filter_->text().trimmed().isEmpty() && visibleConnections().contains(connection))
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
    const auto& visible = visibleConnections();
    return visible.isEmpty() ? 0 : visible.first();
}
bool NavigatorController::hasSelectedConnection() const {
    return !visibleConnections().isEmpty();
}
const QList<quint64>& NavigatorController::visibleConnections() const {
    return static_cast<SelectedConnectionProxy*>(proxy_)->visible();
}
bool NavigatorController::isVisibleConnection(quint64 connection) const {
    return visibleConnections().contains(connection);
}
} // namespace choscordb
