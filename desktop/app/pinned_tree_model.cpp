#include "app/pinned_tree_model.h"
#include "models/navigator_model.h"

#include <algorithm>
#include <utility>

namespace choscordb {
struct PinnedTreeModel::Entry {
    Entry* parent = nullptr;
    int row = 0;
    const void* sourceKey = nullptr; // Registration key; survives source invalidation.
    PinRecord pin;
    QString key;
    QString status;
    QPersistentModelIndex source;
    std::vector<std::unique_ptr<Entry>> children;
};
namespace {
template <typename Entries> void renumber(Entries& entries, std::size_t from) {
    for (auto row = from; row < entries.size(); ++row)
        entries[row]->row = static_cast<int>(row);
}
} // namespace
PinnedTreeModel::PinnedTreeModel(NavigatorModel* source, QObject* parent)
    : QAbstractItemModel(parent), source_(source) {
    connect(source_, &QAbstractItemModel::rowsAboutToBeInserted, this,
            [this](const QModelIndex& parent, int first, int last) {
                pendingInsertParents_ = findSources(parent);
                pendingInsertFirst_ = first;
                pendingInsertLast_ = last;
            });
    connect(source_, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex& parent, int first, int last) {
                if (first != pendingInsertFirst_ || last != pendingInsertLast_)
                    return;
                auto parents = std::exchange(pendingInsertParents_, {});
                pendingInsertFirst_ = pendingInsertLast_ = -1;
                for (auto* entry : parents) {
                    const auto wasUpdating = std::exchange(updating_, true);
                    beginInsertRows(indexFor(entry), first, last);
                    for (int row = first; row <= last; ++row)
                        entry->children.insert(
                            entry->children.begin() + row,
                            makeEntry(source_->index(row, 0, parent), entry, row));
                    renumber(entry->children, static_cast<std::size_t>(last) + 1);
                    endInsertRows();
                    updating_ = wasUpdating;
                }
            });
    connect(source_, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex& parent, int first, int last) {
                for (const auto& root : roots_)
                    if (sourceRowsIncludeRoot(parent, first, last, root.get()))
                        clearResolution(root.get());
                pendingRemoveParents_ = findSources(parent);
                pendingRemoveFirst_ = first;
                pendingRemoveLast_ = last;
            });
    connect(source_, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex&, int first, int last) {
                if (first != pendingRemoveFirst_ || last != pendingRemoveLast_)
                    return;
                auto parents = std::exchange(pendingRemoveParents_, {});
                pendingRemoveFirst_ = pendingRemoveLast_ = -1;
                for (auto* entry : parents) {
                    const auto wasUpdating = std::exchange(updating_, true);
                    beginRemoveRows(indexFor(entry), first, last);
                    for (int row = first; row <= last; ++row)
                        unregisterSources(entry->children[static_cast<std::size_t>(row)].get());
                    entry->children.erase(entry->children.begin() + first,
                                          entry->children.begin() + last + 1);
                    renumber(entry->children, static_cast<std::size_t>(first));
                    endRemoveRows();
                    updating_ = wasUpdating;
                }
            });
    connect(source_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& topLeft, const QModelIndex& bottomRight,
                   const QList<int>& roles) {
                if (entriesBySource_.isEmpty())
                    return;
                for (int row = topLeft.row(); row <= bottomRight.row(); ++row)
                    for (auto* entry : findSources(source_->index(row, 0, topLeft.parent()))) {
                        const auto proxy = indexFor(entry);
                        emit dataChanged(proxy, proxy, roles);
                    }
            });
    connect(source_, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        beginResetModel();
        entriesBySource_.clear();
        for (const auto& root : roots_) {
            root->children.clear();
            root->source = QPersistentModelIndex();
            root->sourceKey = nullptr;
        }
    });
    connect(source_, &QAbstractItemModel::modelReset, this, [this] { endResetModel(); });
}
PinnedTreeModel::~PinnedTreeModel() = default;
QModelIndex PinnedTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (row < 0 || column != 0 || (parent.isValid() && !entry(parent)))
        return {};
    const auto& siblings = parent.isValid() ? entry(parent)->children : roots_;
    return static_cast<size_t>(row) < siblings.size() ? createIndex(row, 0, siblings[row].get())
                                                      : QModelIndex();
}
QModelIndex PinnedTreeModel::parent(const QModelIndex& index) const {
    const auto* value = entry(index);
    return value ? indexFor(value->parent) : QModelIndex();
}
int PinnedTreeModel::rowCount(const QModelIndex& parent) const {
    if (!parent.isValid())
        return static_cast<int>(roots_.size());
    const auto* value = entry(parent);
    return value ? static_cast<int>(value->children.size()) : 0;
}
int PinnedTreeModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() && !entry(parent) ? 0 : 1;
}
QVariant PinnedTreeModel::data(const QModelIndex& index, int role) const {
    const auto* value = entry(index);
    if (!value)
        return {};
    if (value->parent)
        return value->source.data(role);
    const auto& pin = value->pin;
    const auto status = pin.unavailable ? tr("Unavailable") : value->status;
    if (role == Qt::DisplayRole) {
        if (status.isEmpty() || status == tr("Connected") || status == tr("Disconnected") ||
            status == tr("Hidden"))
            return pin.name;
        return tr("%1 — %2").arg(pin.name, status);
    }
    if (role == NavigatorModel::KindRole)
        return pin.kind;
    if (role == NavigatorModel::ObjectIdRole)
        return pin.objectId;
    if (role == NavigatorModel::QualifiedNameRole)
        return pin.qualifiedName;
    QString context = pin.profileName;
    for (const auto& ancestor : pin.ancestryNames)
        if (!ancestor.isEmpty() && ancestor != pin.profileName)
            context += QStringLiteral(" / ") + ancestor;
    if (!pin.qualifiedName.isEmpty())
        context += QStringLiteral(" · ") + pin.qualifiedName;
    if (!pin.relationSubtype.isEmpty())
        context += QStringLiteral(" · ") + pin.relationSubtype;
    if (role == Qt::ToolTipRole)
        return tr("%1 · %2\n%3 · %4").arg(pin.name, pin.kind, context, status);
    if (role == Qt::AccessibleDescriptionRole)
        return tr("%1 %2 in %3. %4. Activate to reveal the original object.")
            .arg(pin.kind, pin.name, context, status);
    return value->source.isValid() ? value->source.data(role) : QVariant();
}
bool PinnedTreeModel::hasChildren(const QModelIndex& parent) const {
    if (!parent.isValid())
        return !roots_.empty();
    const auto* value = entry(parent);
    if (!value)
        return false;
    if (value->parent)
        return !value->children.empty() || source_->hasChildren(value->source);
    if (value->pin.unavailable)
        return false;
    if (value->source.isValid())
        return !value->children.empty() || source_->hasChildren(value->source);
    return value->pin.kind == QLatin1String("schema") ||
           value->pin.kind == QLatin1String("table") || value->pin.kind == QLatin1String("view");
}
bool PinnedTreeModel::canFetchMore(const QModelIndex& parent) const {
    const auto* value = entry(parent);
    return !updating_ && value && value->source.isValid() && source_->canFetchMore(value->source);
}
void PinnedTreeModel::fetchMore(const QModelIndex& parent) {
    if (canFetchMore(parent))
        source_->fetchMore(entry(parent)->source);
}
void PinnedTreeModel::setPins(const QList<PinRecord>& pins) {
    QStringList keys;
    keys.reserve(pins.size());
    for (const auto& pin : pins)
        keys.append(PinStore::identityKey(pin));
    bool sameOrder = pins.size() == static_cast<qsizetype>(roots_.size());
    for (qsizetype row = 0; sameOrder && row < pins.size(); ++row)
        sameOrder = roots_[row]->key == keys[row];
    if (sameOrder) {
        for (qsizetype row = 0; row < pins.size(); ++row) {
            if (roots_[row]->pin.parentObjectId != pins[row].parentObjectId ||
                roots_[row]->pin.ancestryIds != pins[row].ancestryIds || pins[row].unavailable)
                clearResolution(roots_[row].get());
            roots_[row]->pin = pins[row];
        }
        if (!pins.isEmpty())
            emit dataChanged(index(0, 0), index(static_cast<int>(pins.size()) - 1, 0));
        return;
    }
    beginResetModel();
    roots_.clear();
    rootsByKey_.clear();
    entriesBySource_.clear();
    roots_.reserve(static_cast<size_t>(pins.size()));
    for (qsizetype row = 0; row < pins.size(); ++row) {
        auto root = std::make_unique<Entry>();
        root->pin = pins[row];
        root->key = keys[row];
        root->row = static_cast<int>(row);
        if (!rootsByKey_.contains(root->key))
            rootsByKey_.insert(root->key, root.get());
        roots_.push_back(std::move(root));
    }
    endResetModel();
}
bool PinnedTreeModel::setResolved(const QString& key, const QModelIndex& verifiedSourceIndex) {
    auto* root = findRoot(key);
    if (!root)
        return false;
    if (!verifiedSourceIndex.isValid()) {
        clearResolution(root);
        return true;
    }
    if (root->pin.unavailable || verifiedSourceIndex.model() != source_ ||
        verifiedSourceIndex.column() != 0 ||
        verifiedSourceIndex.data(NavigatorModel::ObjectIdRole).toString() != root->pin.objectId ||
        verifiedSourceIndex.data(NavigatorModel::KindRole).toString() != root->pin.kind ||
        verifiedSourceIndex.data(NavigatorModel::QualifiedNameRole).toString() !=
            root->pin.qualifiedName ||
        verifiedSourceIndex.parent().data(NavigatorModel::ObjectIdRole).toString() !=
            root->pin.parentObjectId) {
        clearResolution(root);
        return false;
    }
    if (NavigatorModel::relationSubtype(
            verifiedSourceIndex.data(NavigatorModel::PropertiesRole).toList()) !=
        root->pin.relationSubtype) {
        clearResolution(root);
        return false;
    }
    QStringList ancestry;
    for (auto ancestor = verifiedSourceIndex.parent(); ancestor.isValid();
         ancestor = ancestor.parent())
        if (ancestor.data(NavigatorModel::KindRole).toString() != QLatin1String("connection"))
            ancestry.prepend(ancestor.data(NavigatorModel::ObjectIdRole).toString());
    if (ancestry != root->pin.ancestryIds) {
        clearResolution(root);
        return false;
    }
    if (root->source == verifiedSourceIndex)
        return true;
    clearResolution(root);
    root->source = verifiedSourceIndex;
    root->sourceKey = verifiedSourceIndex.internalPointer();
    entriesBySource_.insert(root->sourceKey, root);
    const auto count = source_->rowCount(verifiedSourceIndex);
    if (count > 0) {
        const auto wasUpdating = std::exchange(updating_, true);
        beginInsertRows(indexFor(root), 0, count - 1);
        root->children.reserve(static_cast<size_t>(count));
        for (int row = 0; row < count; ++row)
            root->children.push_back(
                makeEntry(source_->index(row, 0, verifiedSourceIndex), root, row));
        endInsertRows();
        updating_ = wasUpdating;
    }
    const auto modelIndex = indexFor(root);
    emit dataChanged(modelIndex, modelIndex);
    return true;
}
void PinnedTreeModel::setStatus(const QString& key, const QString& status) {
    auto* root = findRoot(key);
    if (!root || root->status == status)
        return;
    root->status = status;
    const auto modelIndex = indexFor(root);
    emit dataChanged(modelIndex, modelIndex,
                     {Qt::DisplayRole, Qt::ToolTipRole, Qt::AccessibleDescriptionRole});
}
QModelIndex PinnedTreeModel::sourceIndex(const QModelIndex& index) const {
    const auto* value = entry(index);
    return value ? QModelIndex(value->source) : QModelIndex();
}
QString PinnedTreeModel::pinKey(const QModelIndex& root) const {
    const auto* value = entry(root);
    return value && !value->parent ? value->key : QString();
}
bool PinnedTreeModel::isPinnedRoot(const QModelIndex& index) const {
    const auto* value = entry(index);
    return value && !value->parent;
}
PinnedTreeModel::Entry* PinnedTreeModel::entry(const QModelIndex& index) const {
    return index.isValid() && index.model() == this && index.column() == 0
               ? static_cast<Entry*>(index.internalPointer())
               : nullptr;
}
PinnedTreeModel::Entry* PinnedTreeModel::findRoot(const QString& key) const {
    return rootsByKey_.value(key, nullptr);
}
std::vector<PinnedTreeModel::Entry*>
PinnedTreeModel::findSources(const QModelIndex& sourceIndex) const {
    std::vector<Entry*> matches;
    if (!sourceIndex.isValid())
        return matches;
    const auto* key = sourceIndex.internalPointer();
    for (auto it = entriesBySource_.constFind(key);
         it != entriesBySource_.cend() && it.key() == key; ++it)
        if (it.value()->source == sourceIndex)
            matches.push_back(it.value());
    return matches;
}
QModelIndex PinnedTreeModel::indexFor(Entry* value) const {
    return value ? createIndex(value->row, 0, value) : QModelIndex();
}
std::unique_ptr<PinnedTreeModel::Entry> PinnedTreeModel::makeEntry(const QModelIndex& sourceIndex,
                                                                   Entry* parent, int row) {
    auto value = std::make_unique<Entry>();
    value->parent = parent;
    value->row = row;
    value->source = sourceIndex;
    value->sourceKey = sourceIndex.internalPointer();
    entriesBySource_.insert(value->sourceKey, value.get());
    const auto count = source_->rowCount(sourceIndex);
    value->children.reserve(static_cast<size_t>(count));
    for (int child = 0; child < count; ++child)
        value->children.push_back(
            makeEntry(source_->index(child, 0, sourceIndex), value.get(), child));
    return value;
}
void PinnedTreeModel::unregisterSources(Entry* value) {
    std::vector<Entry*> stack{value};
    while (!stack.empty()) {
        auto* current = stack.back();
        stack.pop_back();
        if (current->sourceKey)
            entriesBySource_.remove(std::exchange(current->sourceKey, nullptr), current);
        for (const auto& child : current->children)
            stack.push_back(child.get());
    }
}
void PinnedTreeModel::clearResolution(Entry* root) {
    if (!root)
        return;
    unregisterSources(root);
    if (!root->children.empty()) {
        const auto wasUpdating = std::exchange(updating_, true);
        beginRemoveRows(indexFor(root), 0, static_cast<int>(root->children.size()) - 1);
        root->children.clear();
        endRemoveRows();
        updating_ = wasUpdating;
    }
    root->source = QPersistentModelIndex();
    const auto modelIndex = indexFor(root);
    emit dataChanged(modelIndex, modelIndex);
}
bool PinnedTreeModel::sourceRowsIncludeRoot(const QModelIndex& parent, int first, int last,
                                            const Entry* root) const {
    if (!root->source.isValid())
        return false;
    for (auto ancestor = QModelIndex(root->source); ancestor.isValid();
         ancestor = ancestor.parent())
        if (ancestor.parent() == parent)
            return ancestor.row() >= first && ancestor.row() <= last;
    return false;
}
} // namespace choscordb
