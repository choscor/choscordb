#pragma once

#include "app/pin_store.h"

#include <QAbstractItemModel>
#include <QPersistentModelIndex>
#include <memory>
#include <vector>

namespace choscordb {

class NavigatorModel;

// Presents saved shortcuts as roots and their verified, live navigator rows as children.
class PinnedTreeModel final : public QAbstractItemModel {
    Q_OBJECT
  public:
    explicit PinnedTreeModel(NavigatorModel* source, QObject* parent = nullptr);
    ~PinnedTreeModel() override;

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool hasChildren(const QModelIndex& parent = {}) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    void setPins(const QList<PinRecord>& pins);
    bool setResolved(const QString& key, const QModelIndex& verifiedSourceIndex);
    void setStatus(const QString& key, const QString& status);
    QModelIndex sourceIndex(const QModelIndex& index) const;
    QString pinKey(const QModelIndex& root) const;
    bool isPinnedRoot(const QModelIndex& index) const;

  private:
    struct Entry;
    Entry* entry(const QModelIndex& index) const;
    Entry* findRoot(const QString& key) const;
    std::vector<Entry*> findSources(const QModelIndex& sourceIndex) const;
    QModelIndex indexFor(Entry* entry) const;
    std::unique_ptr<Entry> makeEntry(const QModelIndex& sourceIndex, Entry* parent) const;
    void clearResolution(Entry* root);
    bool sourceRowsIncludeRoot(const QModelIndex& parent, int first, int last,
                               const Entry* root) const;

    NavigatorModel* source_;
    std::vector<std::unique_ptr<Entry>> roots_;
    std::vector<Entry*> pendingInsertParents_;
    int pendingInsertFirst_ = -1;
    int pendingInsertLast_ = -1;
    std::vector<Entry*> pendingRemoveParents_;
    int pendingRemoveFirst_ = -1;
    int pendingRemoveLast_ = -1;
    bool updating_ = false;
};

} // namespace choscordb
