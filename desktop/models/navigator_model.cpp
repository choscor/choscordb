#include "models/navigator_model.h"
#include "bridge/engine_adapter.h"
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
    Node* parent = nullptr;
    int row = 0;
    bool placeholder = false;
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
    for (const auto& root : roots_)
        if (driverResolver_ &&
            driverResolver_(root->connection)
                    .compare(QLatin1String("postgres"), Qt::CaseInsensitive) == 0)
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
            driver, EngineAdapter::navigatorObjectVisible(driver, showSystemSchemas_, {}));
    return visible.value() || !hiddenBySystemSchema(value);
}
bool NavigatorModel::hiddenBySystemSchema(const Node* value) const {
    // Names and kinds of loaded schema rows never change, so the ancestor walk is cached.
    if (value->systemSchema < 0)
        value->systemSchema = (value->parent && hiddenBySystemSchema(value->parent)) ||
                              (value->object.kind == QLatin1String("schema") &&
                               EngineAdapter::postgresSystemSchema(value->object.name));
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
CompletionSnapshot NavigatorModel::completionSnapshot(quint64 connection, quint64 maxEntries,
                                                      quint64 maxUtf8Bytes) const {
    CompletionSnapshot snapshot;
    // Bound ignored nodes and root lookup as well as accepted completion entries.
    const quint64 visitLimit = maxEntries > 12492 ? 100000 : maxEntries * 8 + 64;
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
    quint64 remaining = maxUtf8Bytes;
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
        if (object.kind != "database" && object.kind != "schema" && object.kind != "table" &&
            object.kind != "view" && object.kind != "column")
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
        if (value->object.kind == QLatin1String("column"))
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
        return value->object.kind == QLatin1String("column") ? value->object.databaseType
                                                             : QString();
    case ErrorRole:
        return value->error;
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
    // Failed nodes keep their error row until explicit refresh. An automatic
    // retry during QTreeView expansion would remove the row being laid out.
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
void NavigatorModel::fetchMore(const QModelIndex& parent) {
    if (!canFetchMore(parent))
        return;
    auto* value = node(parent);
    if (!value->children.empty() && value->children.back()->placeholder) {
        const auto last = static_cast<int>(value->children.size()) - 1;
        beginRemoveRows(parent, last, last);
        value->children.pop_back();
        endRemoveRows();
    }
    value->state = Node::Loading;
    value->token = ++nextToken_;
    value->error.clear();
    auto loading = std::make_unique<Node>();
    loading->connection = value->connection;
    loading->parent = value;
    loading->placeholder = true;
    loading->state = Node::Loaded;
    loading->object = {QString(), tr("Loading…"), QString(), QString("loading"), false};
    const auto loadingRow = static_cast<int>(value->children.size());
    loading->row = loadingRow;
    beginInsertRows(parent, loadingRow, loadingRow);
    value->children.push_back(std::move(loading));
    endInsertRows();
    emit dataChanged(parent, parent, {ErrorRole, ChildrenLoadedRole});
    const auto connection = value->connection;
    const auto objectId = value->object.id;
    const auto token = value->token;
    // QTreeView calls fetchMore() from inside its expansion layout. Defer the
    // request boundary so even an immediate reply cannot mutate rows reentrantly.
    QTimer::singleShot(0, this, [this, connection, objectId, token] {
        const auto* current = find(connection, objectId);
        if (current && current->state == Node::Loading && current->token == token) {
            if (current->offset == 0)
                emit childrenRequested(connection, objectId, token);
            else
                emit childrenPageRequested(connection, objectId, token, current->offset, 1000);
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
        return root->connection == id && root->object.kind == "connection";
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
    root->object.name = tr("%1 — Loading…").arg(label);
    root->object.kind = "loading";
    root->object.hasChildren = false;
    root->state = Node::Loaded;
    const auto rootIndex = indexFor(root);
    emit dataChanged(rootIndex, rootIndex, {Qt::DisplayRole, KindRole});
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
    if (hasMore && (nextOffset <= offset || children.empty())) {
        failChildren(connection, parentObjectId, token, tr("Invalid metadata continuation."));
        return false;
    }
    if (children.size() >= std::numeric_limits<int>::max() - value->children.size()) {
        failChildren(connection, parentObjectId, token, tr("Too many metadata objects."));
        return false;
    }
    QSet<QString> ids;
    for (const auto& child : value->children)
        if (!child->placeholder)
            ids.insert(child->object.id);
    for (const auto& object : children) {
        const auto* existing = find(connection, object.id);
        const bool sameIndex = existing && existing->parent != value && object.kind == "index" &&
                               existing->object.kind == "index" && !object.hasChildren &&
                               !existing->object.hasChildren;
        if (object.id.isEmpty() || ids.contains(object.id) || (existing && !sameIndex)) {
            failChildren(connection, parentObjectId, token,
                         tr("Metadata object identifiers must be unique and nonempty."));
            return false;
        }
        ids.insert(object.id);
    }
    std::vector<std::unique_ptr<Node>> prepared;
    prepared.reserve(children.size());
    for (auto& object : children) {
        auto child = std::make_unique<Node>();
        child->connection = connection;
        child->parent = value;
        child->object = std::move(object);
        prepared.push_back(std::move(child));
    }
    const auto parentIndex = indexFor(value);
    const auto loadingRow = static_cast<int>(value->children.size()) - 1;
    beginRemoveRows(parentIndex, loadingRow, loadingRow);
    value->children.pop_back();
    endRemoveRows();
    value->state = Node::Loaded;
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
    emit dataChanged(parentIndex, parentIndex, {ErrorRole, ChildrenLoadedRole, HasMoreRole});
    emit completionChanged(connection);
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
    auto& placeholder = value->children.back();
    placeholder->error = value->error;
    placeholder->object.name = tr("Failed: %1 — refresh to retry").arg(value->error);
    placeholder->object.kind = "error";
    const auto parentIndex = indexFor(value);
    emit dataChanged(indexFor(placeholder.get()), indexFor(placeholder.get()),
                     {Qt::DisplayRole, KindRole, ErrorRole});
    emit dataChanged(parentIndex, parentIndex, {ErrorRole, ChildrenLoadedRole});
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
    value->error.clear();
    emit completionChanged(value->connection);
    fetchMore(indexFor(value));
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
