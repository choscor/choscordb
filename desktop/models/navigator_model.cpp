#include "models/navigator_model.h"
#include <QSet>
#include <QTimer>
#include <algorithm>
#include <limits>
namespace choscordb {
struct NavigatorModel::Node {
    enum State { Unloaded, Loading, Loaded, Failed } state = Unloaded;
    quint64 connection = 0;
    quint64 token = 0;
    quint64 offset = 0;
    bool hasMore = false;
    NavigatorObject object;
    QString error;
    RowStatus status;
    Node* parent = nullptr;
    int row = 0;
    bool placeholder = false;
    // The children are saved children that the live page has not replaced yet.
    bool cached = false;
    // Set from Rust's kind traits when the node is created, so data() never asks.
    bool column = false;
    bool sidebarChild = true;
    // -1 unknown; otherwise whether this node or an ancestor is a hidden system schema.
    mutable signed char systemSchema = -1;
    std::vector<std::unique_ptr<Node>> children;
};
NavigatorModel::NavigatorModel(QObject* parent) : QAbstractItemModel(parent) {}
NavigatorModel::~NavigatorModel() = default;
void NavigatorModel::setDriverResolver(std::function<QString(quint64)> resolver) {
    driverResolver_ = std::move(resolver);
    unverifiedVisibleByDriver_.clear();
    for (const auto& root : roots_)
        emit completionChanged(root->connection);
}
void NavigatorModel::setShowSystemSchemas(bool show) {
    if (showSystemSchemas_ == show)
        return;
    showSystemSchemas_ = show;
    unverifiedVisibleByDriver_.clear();
    // Only drivers whose objects the system-schema policy can hide change completions.
    for (const auto& root : roots_)
        if (driverResolver_ &&
            EngineAdapter::systemSchemasHidden(driverResolver_(root->connection), false))
            emit completionChanged(root->connection);
}
bool NavigatorModel::isBrowsable(const QModelIndex& index) const {
    return isBrowsable(node(index));
}
bool NavigatorModel::canShowUnverifiedObject(quint64 connection,
                                             const QString& qualifiedName) const {
    return EngineAdapter::navigatorObjectVisible(driverResolver_ ? driverResolver_(connection)
                                                                 : QString{},
                                                 showSystemSchemas_, qualifiedName);
}
quint64 NavigatorModel::pendingRequestToken(const QModelIndex& index) const {
    const auto* value = node(index);
    return value && value->state == Node::Loading ? value->token : 0;
}
bool NavigatorModel::isBrowsable(const Node* value) const {
    if (!value)
        return true;
    const auto driver = driverResolver_ ? driverResolver_(value->connection) : QString{};
    auto visible = unverifiedVisibleByDriver_.constFind(driver);
    if (visible == unverifiedVisibleByDriver_.cend())
        visible = unverifiedVisibleByDriver_.insert(
            driver, !EngineAdapter::systemSchemasHidden(driver, showSystemSchemas_));
    return visible.value() || !hiddenBySystemSchema(value);
}
bool NavigatorModel::hiddenBySystemSchema(const Node* value) const {
    // Names and kinds of loaded schema rows never change, so the ancestor walk is cached.
    if (value->systemSchema < 0)
        value->systemSchema =
            (value->parent && hiddenBySystemSchema(value->parent)) ||
            EngineAdapter::systemSchemaNode(value->object.kind, value->object.name);
    return value->systemSchema > 0;
}
QString NavigatorModel::relationSubtype(const QVariantList& properties) {
    for (const auto& entry : properties) {
        const auto property = entry.toMap();
        if (property.value(QStringLiteral("name")).toString() == QStringLiteral("Relation subtype"))
            return property.value(QStringLiteral("value")).toString();
    }
    return {};
}
void NavigatorModel::registerNode(Node* value) {
    if (!value->placeholder)
        nodesById_[value->connection].insert(value->object.id, value);
}
void NavigatorModel::unregisterSubtree(const Node* value) {
    auto ids = nodesById_.find(value->connection);
    if (ids == nodesById_.end())
        return;
    std::vector<const Node*> stack{value};
    while (!stack.empty()) {
        const auto* current = stack.back();
        stack.pop_back();
        if (!current->placeholder)
            ids->remove(current->object.id, const_cast<Node*>(current));
        for (const auto& child : current->children)
            stack.push_back(child.get());
    }
}
CompletionSnapshot NavigatorModel::completionSnapshot(quint64 connection,
                                                      const CompletionLimits& limits) const {
    CompletionSnapshot snapshot;
    const auto maxEntries = limits.maxMetadataEntries;
    // Bound ignored nodes and root lookup as well as accepted completion entries.
    const quint64 visitLimit = limits.maxMetadataVisits;
    quint64 visited = 0;
    const Node* root = nullptr;
    for (const auto& candidate : roots_) {
        if (++visited > visitLimit) {
            snapshot.partial = true;
            return snapshot;
        }
        if (candidate->connection == connection) {
            root = candidate.get();
            break;
        }
    }
    if (!root)
        return snapshot;
    quint64 remaining = limits.maxMetadataBytes;
    auto charge = [](const QString& text, quint64& budget) {
        // Count UTF-8 without allocating an unbounded conversion of a stored label.
        for (qsizetype i = 0; i < text.size(); ++i) {
            const auto unit = text[i].unicode();
            quint64 bytes = unit < 0x80 ? 1 : unit < 0x800 ? 2 : 3;
            if (text[i].isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
                bytes = 4;
                ++i;
            }
            if (bytes > budget)
                return false;
            budget -= bytes;
        }
        return true;
    };
    auto owned = [](const QString& text) {
        QString copy(text.constData(), text.size());
        copy.squeeze();
        return copy;
    };
    struct Frame {
        const Node* node;
        size_t child = 0;
    };
    std::vector<Frame> stack;
    stack.push_back({root});
    auto visit = [&](const Node* value) {
        if (value->placeholder)
            return true;
        if (value->object.hasChildren && (value->state != Node::Loaded || value->hasMore))
            snapshot.partial = true;
        const auto& object = value->object;
        if (!EngineAdapter::objectKindTraits(object.kind).completionCandidate)
            return true;
        if (snapshot.objects.size() >= maxEntries) {
            snapshot.partial = true;
            return false;
        }
        auto budget = remaining;
        if (!charge(object.id, budget) || !charge(object.name, budget) ||
            !charge(object.qualifiedName, budget) || !charge(object.kind, budget)) {
            snapshot.partial = true;
            return false;
        }
        remaining = budget;
        snapshot.objects.push_back({owned(object.id), owned(object.name),
                                    owned(object.qualifiedName), owned(object.kind),
                                    object.hasChildren});
        return true;
    };
    if (!visit(root))
        return snapshot;
    while (!stack.empty()) {
        auto& frame = stack.back();
        if (frame.child == frame.node->children.size()) {
            stack.pop_back();
            continue;
        }
        const auto* child = frame.node->children[frame.child++].get();
        if (!isBrowsable(child))
            continue;
        if (++visited > visitLimit) {
            snapshot.partial = true;
            break;
        }
        if (!visit(child))
            break;
        if (!child->placeholder && !child->children.empty())
            stack.push_back({child});
    }
    return snapshot;
}
NavigatorModel::Node* NavigatorModel::node(const QModelIndex& index) const {
    return index.isValid() && index.model() == this && index.column() == 0
               ? static_cast<Node*>(index.internalPointer())
               : nullptr;
}
QModelIndex NavigatorModel::indexFor(Node* value) const {
    return value ? createIndex(value->row, 0, value) : QModelIndex();
}
QModelIndex NavigatorModel::index(int row, int column, const QModelIndex& parent) const {
    if (row < 0 || column != 0 || (parent.isValid() && !node(parent)))
        return {};
    const auto& children = parent.isValid() ? node(parent)->children : roots_;
    if (static_cast<std::size_t>(row) >= children.size())
        return {};
    return createIndex(row, column, children[row].get());
}
QModelIndex NavigatorModel::parent(const QModelIndex& index) const {
    const auto* value = node(index);
    return value ? indexFor(value->parent) : QModelIndex();
}
int NavigatorModel::rowCount(const QModelIndex& parent) const {
    if (!parent.isValid())
        return static_cast<int>(roots_.size());
    const auto* value = node(parent);
    return value ? static_cast<int>(value->children.size()) : 0;
}
int NavigatorModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() && !node(parent) ? 0 : 1;
}
QVariant NavigatorModel::data(const QModelIndex& index, int role) const {
    const auto* value = node(index);
    if (!value)
        return {};
    switch (role) {
    case Qt::DisplayRole:
        return value->object.name;
    case Qt::ToolTipRole:
    case Qt::AccessibleDescriptionRole:
        if (value->status.kind != RowStatusKind::None)
            return value->status.message;
        if (value->column)
            return value->object.databaseType.isEmpty()
                       ? value->object.name
                       : QStringLiteral("%1 — %2").arg(value->object.name,
                                                       value->object.databaseType);
        return {};
    case ConnectionRole:
        return QVariant::fromValue(value->connection);
    case ObjectIdRole:
        return value->object.id;
    case QualifiedNameRole:
        return value->object.qualifiedName;
    case KindRole:
        return value->object.kind;
    case ChildrenLoadedRole:
        return !value->placeholder && value->state == Node::Loaded && !value->hasMore;
    case HasMoreRole:
        return !value->placeholder && value->hasMore && value->state == Node::Loaded;
    case PropertiesRole:
        return value->object.properties;
    case DatabaseTypeRole:
        return value->column ? value->object.databaseType : QString();
    case ErrorRole:
        return value->error;
    case StatusRole:
        return int(value->status.kind);
    case RetryRole:
        return int(value->status.retry);
    case SidebarChildRole:
        return value->sidebarChild;
    default:
        return {};
    }
}
bool NavigatorModel::hasChildren(const QModelIndex& parent) const {
    if (!parent.isValid())
        return !roots_.empty();
    const auto* value = node(parent);
    return value && (!value->children.empty() ||
                     (value->object.hasChildren && value->state != Node::Loaded));
}
bool NavigatorModel::canFetchMore(const QModelIndex& parent) const {
    const auto* value = node(parent);
    // Failed nodes stay failed until Refresh or collapse. A view that probes
    // again during layout must not start a retry loop.
    return value && !value->placeholder && value->object.hasChildren &&
           value->state == Node::Unloaded && nextToken_ < std::numeric_limits<quint64>::max();
}
void NavigatorModel::clearChildren(Node* value) {
    if (value->children.empty())
        return;
    beginRemoveRows(indexFor(value), 0, static_cast<int>(value->children.size()) - 1);
    for (const auto& child : value->children)
        unregisterSubtree(child.get());
    value->children.clear();
    endRemoveRows();
}
void NavigatorModel::setStatus(Node* value, RowStatusCause cause, const QString& detail) {
    value->status = EngineAdapter::rowStatus(cause, detail);
    const auto index = indexFor(value);
    emit dataChanged(index, index,
                     {StatusRole, RetryRole, ErrorRole, ChildrenLoadedRole, Qt::ToolTipRole,
                      Qt::AccessibleDescriptionRole});
}
void NavigatorModel::fetchMore(const QModelIndex& parent) {
    request(node(parent), false);
}
void NavigatorModel::request(Node* value, bool fresh) {
    const auto parent = indexFor(value);
    if (!canFetchMore(parent))
        return;
    if (!value->children.empty() && value->children.back()->placeholder) {
        const auto last = static_cast<int>(value->children.size()) - 1;
        beginRemoveRows(parent, last, last);
        value->children.pop_back();
        endRemoveRows();
    }
    value->state = Node::Loading;
    value->token = ++nextToken_;
    value->error.clear();
    setStatus(value, RowStatusCause::ChildrenLoading);
    const auto connection = value->connection;
    const auto objectId = value->object.id;
    const auto token = value->token;
    // QTreeView calls fetchMore() from inside its expansion layout. Defer the
    // request boundary so even an immediate reply cannot mutate rows reentrantly.
    QTimer::singleShot(0, this, [this, connection, objectId, token, fresh] {
        const auto* current = find(connection, objectId);
        if (current && current->state == Node::Loading && current->token == token) {
            if (current->offset == 0)
                emit childrenRequested(connection, objectId, token, fresh);
            else
                emit childrenPageRequested(connection, objectId, token, current->offset);
        }
    });
}
NavigatorModel::Node* NavigatorModel::find(quint64 connection, const QString& id) const {
    const auto ids = nodesById_.constFind(connection);
    return ids == nodesById_.cend() ? nullptr : ids->value(id, nullptr);
}
bool NavigatorModel::addConnection(quint64 id, const QString& label) {
    if (find(id, QString()) || roots_.size() >= std::numeric_limits<int>::max())
        return false;
    auto value = std::make_unique<Node>();
    value->connection = id;
    value->object = {QString(), label, QString(), QString("connection"), true};
    value->sidebarChild = EngineAdapter::sidebarChildVisible(QString(), value->object.kind);
    const auto row = static_cast<int>(roots_.size());
    value->row = row;
    beginInsertRows({}, row, row);
    registerNode(value.get());
    roots_.push_back(std::move(value));
    endInsertRows();
    return true;
}
bool NavigatorModel::renameConnection(quint64 id, const QString& label) {
    const auto it = std::find_if(roots_.begin(), roots_.end(), [id](const auto& root) {
        return root->connection == id &&
               EngineAdapter::objectKindTraits(root->object.kind).connection;
    });
    if (it == roots_.end())
        return false;
    if ((*it)->object.name == label)
        return true;
    (*it)->object.name = label;
    const auto rootIndex = indexFor(it->get());
    emit dataChanged(rootIndex, rootIndex, {Qt::DisplayRole});
    return true;
}
bool NavigatorModel::addPendingConnection(quint64 id, const QString& label) {
    if (!addConnection(id, label))
        return false;
    auto* root = find(id, {});
    root->object.hasChildren = false;
    root->state = Node::Loaded;
    setStatus(root, RowStatusCause::Connecting);
    return true;
}
bool NavigatorModel::removeConnection(quint64 id) {
    const auto it = std::find_if(roots_.begin(), roots_.end(),
                                 [id](const auto& n) { return n->connection == id; });
    if (it == roots_.end())
        return false;
    const auto row = static_cast<int>(it - roots_.begin());
    beginRemoveRows({}, row, row);
    nodesById_.remove(id);
    roots_.erase(it);
    for (auto index = static_cast<std::size_t>(row); index < roots_.size(); ++index)
        roots_[index]->row = static_cast<int>(index);
    endRemoveRows();
    emit completionChanged(id);
    return true;
}
bool NavigatorModel::applyChildren(quint64 connection, const QString& parentObjectId, quint64 token,
                                   std::vector<NavigatorObject> children) {
    return applyChildrenPage(connection, parentObjectId, token, std::move(children), 0, false, 0);
}
bool NavigatorModel::applyChildrenPage(quint64 connection, const QString& parentObjectId,
                                       quint64 token, std::vector<NavigatorObject> children,
                                       quint64 offset, bool hasMore, quint64 nextOffset) {
    auto* value = find(connection, parentObjectId);
    if (!value || value->state != Node::Loading || value->token != token || value->offset != offset)
        return false;
    return insertChildren(value, std::move(children), hasMore, nextOffset, false);
}
bool NavigatorModel::applyCachedChildren(quint64 connection, const QString& parentObjectId,
                                         quint64 token, std::vector<NavigatorObject> children) {
    auto* value = find(connection, parentObjectId);
    if (!value || value->state != Node::Loading || value->token != token || value->offset != 0 ||
        value->cached)
        return false;
    return insertChildren(value, std::move(children), false, 0, true);
}
bool NavigatorModel::insertChildren(Node* value, std::vector<NavigatorObject> children,
                                    bool hasMore, quint64 nextOffset, bool cached) {
    const auto connection = value->connection;
    const auto& parentObjectId = value->object.id;
    const auto token = value->token;
    const auto offset = value->offset;
    // A live page replaces the saved children only after it is valid, so a bad page keeps them.
    const bool replacing = value->cached;
    if (hasMore && (nextOffset <= offset || children.empty())) {
        failChildren(connection, parentObjectId, token, tr("Invalid metadata continuation."));
        return false;
    }
    if (children.size() >= std::numeric_limits<int>::max() - value->children.size()) {
        failChildren(connection, parentObjectId, token, tr("Too many metadata objects."));
        return false;
    }
    QSet<QString> ids;
    if (!replacing)
        for (const auto& child : value->children)
            if (!child->placeholder)
                ids.insert(child->object.id);
    for (const auto& object : children) {
        const auto* existing = find(connection, object.id);
        if (replacing && existing && existing->parent == value)
            existing = nullptr;
        // Rust names the kinds one object may list under several parents.
        const bool repeated = existing && existing->parent != value &&
                              object.kind == existing->object.kind &&
                              EngineAdapter::objectKindTraits(object.kind).repeatsAcrossParents &&
                              !object.hasChildren && !existing->object.hasChildren;
        if (object.id.isEmpty() || ids.contains(object.id) || (existing && !repeated)) {
            failChildren(connection, parentObjectId, token,
                         tr("Metadata object identifiers must be unique and nonempty."));
            return false;
        }
        ids.insert(object.id);
    }
    std::vector<std::unique_ptr<Node>> prepared;
    // The live page keeps the saved nodes that it lists, so their expansion stays.
    if (replacing && !reconcileSaved(value, children))
        clearChildren(value);
    if (!replacing || value->children.empty()) {
        prepared.reserve(children.size());
        for (auto& object : children)
            prepared.push_back(makeChild(value, std::move(object)));
    }
    const auto parentIndex = indexFor(value);
    // Saved children keep the node loading until its live page arrives.
    value->cached = cached;
    value->state = cached ? Node::Loading : Node::Loaded;
    value->status = cached ? EngineAdapter::rowStatus(RowStatusCause::ChildrenCached) : RowStatus{};
    value->hasMore = hasMore;
    value->offset = nextOffset;
    value->error.clear();
    if (hasMore) {
        auto continuation = std::make_unique<Node>();
        continuation->connection = connection;
        continuation->parent = value;
        continuation->placeholder = true;
        continuation->state = Node::Loaded;
        continuation->object = {QString(), tr("Load more…"), QString(), QString("load_more"),
                                false};
        continuation->sidebarChild =
            EngineAdapter::sidebarChildVisible(value->object.kind, continuation->object.kind);
        prepared.push_back(std::move(continuation));
    }
    if (!prepared.empty()) {
        const auto first = static_cast<int>(value->children.size());
        beginInsertRows(parentIndex, first, first + static_cast<int>(prepared.size()) - 1);
        value->children.reserve(value->children.size() + prepared.size());
        for (auto& child : prepared) {
            child->row = static_cast<int>(value->children.size());
            registerNode(child.get());
            value->children.push_back(std::move(child));
        }
        endInsertRows();
    }
    emit dataChanged(parentIndex, parentIndex,
                     {ErrorRole, ChildrenLoadedRole, HasMoreRole, StatusRole, RetryRole,
                      Qt::ToolTipRole, Qt::AccessibleDescriptionRole});
    emit completionChanged(connection);
    return true;
}
std::unique_ptr<NavigatorModel::Node> NavigatorModel::makeChild(Node* parent,
                                                                NavigatorObject object) const {
    auto child = std::make_unique<Node>();
    child->connection = parent->connection;
    child->parent = parent;
    child->object = std::move(object);
    child->column = EngineAdapter::objectKindTraits(child->object.kind).column;
    child->sidebarChild =
        EngineAdapter::sidebarChildVisible(parent->object.kind, child->object.kind);
    return child;
}
bool NavigatorModel::reconcileSaved(Node* value, std::vector<NavigatorObject>& children) {
    QHash<QString, std::size_t> listed;
    listed.reserve(static_cast<qsizetype>(children.size()));
    for (std::size_t i = 0; i < children.size(); ++i)
        listed.insert(children[i].id, i);
    const auto keeps = [&](const Node* child) {
        const auto at = listed.constFind(child->object.id);
        return at != listed.cend() && children[*at].kind == child->object.kind &&
               children[*at].hasChildren == child->object.hasChildren;
    };
    const auto parentIndex = indexFor(value);
    auto& rows = value->children;
    const auto renumber = [&rows](std::size_t from) {
        for (auto i = from; i < rows.size(); ++i)
            rows[i]->row = static_cast<int>(i);
    };
    // Remove the saved rows that the live page does not list, one range at a time.
    for (auto last = static_cast<int>(rows.size()) - 1; last >= 0;) {
        if (keeps(rows[last].get())) {
            --last;
            continue;
        }
        auto first = last;
        while (first > 0 && !keeps(rows[first - 1].get()))
            --first;
        beginRemoveRows(parentIndex, first, last);
        for (auto i = first; i <= last; ++i)
            unregisterSubtree(rows[i].get());
        rows.erase(rows.begin() + first, rows.begin() + last + 1);
        renumber(static_cast<std::size_t>(first));
        endRemoveRows();
        last = first - 1;
    }
    // A changed order rebuilds the rows. A refresh of the parent is the same as that.
    std::vector<bool> kept(children.size(), false);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto at = listed.value(rows[i]->object.id);
        if (i > 0 && at <= listed.value(rows[i - 1]->object.id))
            return false;
        kept[at] = true;
    }
    for (const auto& child : rows) {
        auto& object = children[listed.value(child->object.id)];
        // A renamed node, such as a PostgreSQL schema, finds its system-schema state again.
        if (object.name != child->object.name)
            for (std::vector<Node*> stack{child.get()}; !stack.empty();) {
                auto* current = stack.back();
                stack.pop_back();
                current->systemSchema = -1;
                for (const auto& descendant : current->children)
                    stack.push_back(descendant.get());
            }
        child->object = std::move(object);
    }
    if (!rows.empty())
        emit dataChanged(index(0, 0, parentIndex),
                         index(static_cast<int>(rows.size()) - 1, 0, parentIndex));
    // Insert each run of new children at its place.
    std::size_t row = 0;
    for (std::size_t i = 0; i < children.size();) {
        if (kept[i]) {
            ++row;
            ++i;
            continue;
        }
        auto end = i;
        while (end < children.size() && !kept[end])
            ++end;
        const auto first = static_cast<int>(row);
        beginInsertRows(parentIndex, first, first + static_cast<int>(end - i) - 1);
        std::vector<std::unique_ptr<Node>> run;
        run.reserve(end - i);
        for (auto at = i; at < end; ++at) {
            run.push_back(makeChild(value, std::move(children[at])));
            registerNode(run.back().get());
        }
        rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(row),
                    std::make_move_iterator(run.begin()), std::make_move_iterator(run.end()));
        renumber(row);
        endInsertRows();
        row += end - i;
        i = end;
    }
    return true;
}
void NavigatorModel::requestNextPage(const QModelIndex& index) {
    auto* value = node(index);
    if (value && value->placeholder)
        value = value->parent;
    if (!value || !value->hasMore || value->state != Node::Loaded)
        return;
    value->state = Node::Unloaded;
    fetchMore(indexFor(value));
}
bool NavigatorModel::failChildren(quint64 connection, const QString& parentObjectId, quint64 token,
                                  const QString& error) {
    auto* value = find(connection, parentObjectId);
    if (!value || value->state != Node::Loading || value->token != token)
        return false;
    value->state = Node::Failed;
    value->error = error.isEmpty() ? tr("Metadata loading failed.") : error;
    setStatus(value,
              value->cached ? RowStatusCause::ChildrenCachedFailed : RowStatusCause::ChildrenFailed,
              value->error);
    emit completionChanged(connection);
    return true;
}
void NavigatorModel::refresh(const QModelIndex& index) {
    auto* value = node(index);
    if (!value)
        return;
    if (value->placeholder)
        value = value->parent;
    if (!value || !value->object.hasChildren)
        return;
    clearChildren(value);
    value->state = Node::Unloaded;
    value->offset = 0;
    value->hasMore = false;
    value->cached = false;
    value->error.clear();
    emit completionChanged(value->connection);
    request(value, true);
}
bool NavigatorModel::refreshObject(quint64 connection, const QString& objectId) {
    auto* value = find(connection, objectId);
    if (!value || !value->object.hasChildren)
        return false;
    refresh(indexFor(value));
    return true;
}
bool NavigatorModel::matchesObject(quint64 connection, const QString& objectId, const QString& kind,
                                   const QString& qualifiedName, const QString& parentObjectId,
                                   const QString& relationSubtype, bool requireBrowsable) const {
    const auto ids = nodesById_.constFind(connection);
    if (ids == nodesById_.cend() || objectId.isEmpty())
        return false;
    for (auto it = ids->constFind(objectId); it != ids->cend() && it.key() == objectId; ++it) {
        const auto* value = it.value();
        if (!value->parent || value->object.kind != kind ||
            value->object.qualifiedName != qualifiedName ||
            value->parent->object.id != parentObjectId || (requireBrowsable && !isBrowsable(value)))
            continue;
        return NavigatorModel::relationSubtype(value->object.properties) == relationSubtype;
    }
    return false;
}
std::optional<NavigatorObjectSnapshot>
NavigatorModel::objectSnapshot(quint64 connection, const QString& objectId) const {
    const auto ids = nodesById_.constFind(connection);
    if (objectId.isEmpty() || ids == nodesById_.cend())
        return std::nullopt;
    std::optional<NavigatorObjectSnapshot> snapshot;
    for (auto it = ids->constFind(objectId); it != ids->cend() && it.key() == objectId; ++it) {
        const auto* current = it.value();
        if (!current->parent || !isBrowsable(current))
            continue;
        if (snapshot)
            return std::nullopt;
        snapshot = NavigatorObjectSnapshot{connection,
                                           current->object.id,
                                           current->object.name,
                                           current->object.qualifiedName,
                                           current->object.kind,
                                           current->parent->object.id,
                                           current->object.properties};
    }
    return snapshot;
}
} // namespace choscordb
