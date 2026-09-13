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
    NavigatorObject object;
    QString error;
    Node* parent = nullptr;
    bool placeholder = false;
    std::vector<std::unique_ptr<Node>> children;
};
NavigatorModel::NavigatorModel(QObject* parent) : QAbstractItemModel(parent) {}
NavigatorModel::~NavigatorModel() = default;
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
        if (value->object.hasChildren && value->state != Node::Loaded)
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
        if (++visited > visitLimit) {
            snapshot.partial = true;
            break;
        }
        const auto* child = frame.node->children[frame.child++].get();
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
    if (!value)
        return {};
    const auto& siblings = value->parent ? value->parent->children : roots_;
    const auto it = std::find_if(siblings.begin(), siblings.end(),
                                 [value](const auto& n) { return n.get() == value; });
    return it == siblings.end() ? QModelIndex()
                                : createIndex(static_cast<int>(it - siblings.begin()), 0, value);
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
    case ConnectionRole:
        return QVariant::fromValue(value->connection);
    case ObjectIdRole:
        return value->object.id;
    case QualifiedNameRole:
        return value->object.qualifiedName;
    case KindRole:
        return value->object.kind;
    case ChildrenLoadedRole:
        return !value->placeholder && value->state == Node::Loaded;
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
    value->children.clear();
    endRemoveRows();
}
void NavigatorModel::fetchMore(const QModelIndex& parent) {
    if (!canFetchMore(parent))
        return;
    auto* value = node(parent);
    clearChildren(value);
    value->state = Node::Loading;
    value->token = ++nextToken_;
    value->error.clear();
    auto loading = std::make_unique<Node>();
    loading->connection = value->connection;
    loading->parent = value;
    loading->placeholder = true;
    loading->state = Node::Loaded;
    loading->object = {QString(), tr("Loading…"), QString(), QString("loading"), false};
    beginInsertRows(parent, 0, 0);
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
        if (current && current->state == Node::Loading && current->token == token)
            emit childrenRequested(connection, objectId, token);
    });
}
NavigatorModel::Node* NavigatorModel::find(quint64 connection, const QString& id) const {
    const auto root = std::find_if(roots_.begin(), roots_.end(), [connection](const auto& n) {
        return n->connection == connection;
    });
    if (root == roots_.end())
        return nullptr;
    auto search = [&](auto&& self, Node* value) -> Node* {
        if (!value->placeholder && value->object.id == id)
            return value;
        for (const auto& child : value->children)
            if (auto* found = self(self, child.get()))
                return found;
        return nullptr;
    };
    return search(search, root->get());
}
bool NavigatorModel::addConnection(quint64 id, const QString& label) {
    if (find(id, QString()) || roots_.size() >= std::numeric_limits<int>::max())
        return false;
    auto value = std::make_unique<Node>();
    value->connection = id;
    value->object = {QString(), label, QString(), QString("connection"), true};
    const auto row = static_cast<int>(roots_.size());
    beginInsertRows({}, row, row);
    roots_.push_back(std::move(value));
    endInsertRows();
    return true;
}
bool NavigatorModel::removeConnection(quint64 id) {
    const auto it = std::find_if(roots_.begin(), roots_.end(),
                                 [id](const auto& n) { return n->connection == id; });
    if (it == roots_.end())
        return false;
    const auto row = static_cast<int>(it - roots_.begin());
    beginRemoveRows({}, row, row);
    roots_.erase(it);
    endRemoveRows();
    emit completionChanged(id);
    return true;
}
bool NavigatorModel::applyChildren(quint64 connection, const QString& parentObjectId, quint64 token,
                                   std::vector<NavigatorObject> children) {
    auto* value = find(connection, parentObjectId);
    if (!value || value->state != Node::Loading || value->token != token)
        return false;
    if (children.size() > std::numeric_limits<int>::max()) {
        failChildren(connection, parentObjectId, token, tr("Too many metadata objects."));
        return false;
    }
    QSet<QString> ids;
    for (const auto& object : children) {
        if (object.id.isEmpty() || ids.contains(object.id) || find(connection, object.id)) {
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
    clearChildren(value);
    value->state = Node::Loaded;
    value->error.clear();
    const auto parentIndex = indexFor(value);
    if (!prepared.empty()) {
        beginInsertRows(parentIndex, 0, static_cast<int>(prepared.size()) - 1);
        value->children = std::move(prepared);
        endInsertRows();
    }
    emit dataChanged(parentIndex, parentIndex, {ErrorRole, ChildrenLoadedRole});
    emit completionChanged(connection);
    return true;
}
bool NavigatorModel::failChildren(quint64 connection, const QString& parentObjectId, quint64 token,
                                  const QString& error) {
    auto* value = find(connection, parentObjectId);
    if (!value || value->state != Node::Loading || value->token != token)
        return false;
    value->state = Node::Failed;
    value->error = error.isEmpty() ? tr("Metadata loading failed.") : error;
    auto& placeholder = value->children.front();
    placeholder->error = value->error;
    placeholder->object.name = tr("Failed: %1 — refresh to retry").arg(value->error);
    placeholder->object.kind = "error";
    const auto parentIndex = indexFor(value);
    emit dataChanged(indexFor(placeholder.get()), indexFor(placeholder.get()),
                     {Qt::DisplayRole, KindRole, ErrorRole});
    emit dataChanged(parentIndex, parentIndex, {ErrorRole, ChildrenLoadedRole});
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
    value->error.clear();
    emit completionChanged(value->connection);
    fetchMore(indexFor(value));
}
} // namespace choscordb
