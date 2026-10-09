#include "app/navigator_controller.h"
#include "bridge/engine_adapter.h"
#include "bridge/quick_search.h"
#include "bridge/text_filter.h"
#include "models/navigator_model.h"
#include <QLineEdit>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QTreeView>

// Object search over the navigator tree: the sidebar filter and quick-search lookup.
namespace choscordb {
void NavigatorController::startQuickObjectSearch(const QString& query,
                                                 std::optional<quint64> connection, int delayMs) {
    quickObjectQuery_ = query.trimmed();
    quickObjectResults_.clear();
    quickObjectRequests_ = 0;
    quickObjectAwaiting_ = false;
    quickObjectIncomplete_ = !quickObjectQuery_.isEmpty();
    const auto& visible = visibleConnections();
    quickObjectConnectionValid_ = connection ? visible.contains(*connection) : !visible.isEmpty();
    quickObjectConnection_ = connection.value_or(visible.isEmpty() ? 0 : visible.first());
    quickObjectStatus_ = quickObjectQuery_.isEmpty() ? QString{}
                         : !quickObjectConnectionValid_
                             ? tr("Select an available connection to search objects.")
                             : tr("Searching objects…");
    quickObjectSearching_ = !quickObjectQuery_.isEmpty() && quickObjectConnectionValid_;
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
    quickObjectSearching_ = false;
    quickObjectRequests_ = 0;
    quickObjectConnectionValid_ = false;
    ++quickObjectGeneration_;
    emit quickObjectSearchChanged();
}
void NavigatorController::advanceQuickObjectSearch(quint64 generation) {
    if (generation != quickObjectGeneration_ || quickObjectQuery_.isEmpty() ||
        !quickObjectConnectionValid_)
        return;
    const auto& budget = EngineAdapter::navigatorSearchBudget();
    const int visitLimit = budget.quickVisits;
    const int requestLimit = budget.quickRequests;
    const int resultLimit = budget.quickResults;
    const auto& visible = visibleConnections();
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
        if (!EngineAdapter::objectKindTraits(kind).connection &&
            (matches(current.data(Qt::DisplayRole).toString()) ||
             matches(current.data(NavigatorModel::QualifiedNameRole).toString()))) {
            auto target = current;
            QString targetKind = kind;
            while (target.isValid() &&
                   !EngineAdapter::objectKindTraits(targetKind).opensObjectTab) {
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
                result.targetPane = EngineAdapter::objectKindTraits(kind).detailPane;
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
            quickObjectSearching_ = true;
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
    quickObjectSearching_ = !limited && error.isEmpty() && loading;
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
    const auto& visible = visibleConnections();
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
    const TextFilter match(filter_->text());
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
            if (++searchVisited_ > EngineAdapter::navigatorSearchBudget().filterVisits)
                return limitReached();
            const auto kind = current.data(NavigatorModel::KindRole).toString();
            if (kind == "loading" || kind == "error")
                searchIncomplete_ = true;
            if (match.matches(current.data(Qt::DisplayRole).toString()))
                searchMatches_.emplace_back(current);
            if (!EngineAdapter::objectKindTraits(kind).searchDescends)
                continue;
        }
        if (!current.data(NavigatorModel::ErrorRole).toString().isEmpty())
            searchIncomplete_ = true;
        if (model_->canFetchMore(current) || current.data(NavigatorModel::HasMoreRole).toBool()) {
            if (searchRequests_ >= EngineAdapter::navigatorSearchBudget().filterRequests)
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
