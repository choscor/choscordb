#pragma once
#include <QAbstractTableModel>
#include <QByteArray>
#include <cstddef>
#include <vector>
namespace choscordb {
class ValuePreviewModel final : public QAbstractTableModel {
  public:
    explicit ValuePreviewModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}
    bool setChunk(QByteArray bytes, quint64 offset, quint64 totalBytes, bool binary);
    void clear();
    quint64 nextOffset() const { return nextOffset_; }
    std::size_t residentBytes() const;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

  private:
    QByteArray bytes_;
    std::vector<quint32> boundaries_;
    quint64 offset_ = 0;
    quint64 nextOffset_ = 0;
    bool binary_ = false;
};
}
