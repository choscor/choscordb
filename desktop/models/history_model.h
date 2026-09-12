#pragma once
#include "bridge/engine_adapter.h"
#include <QAbstractTableModel>
#include <QHash>
namespace choscordb {
class HistoryModel final : public QAbstractTableModel {
    Q_OBJECT
  public:
    explicit HistoryModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void setEntries(QList<SavedHistoryEntry> entries);
    void setProfileNames(QHash<QString, QString> names);
    const SavedHistoryEntry* entry(int row) const;

  private:
    QList<SavedHistoryEntry> entries_;
    QHash<QString, QString> profileNames_;
};
} // namespace choscordb
