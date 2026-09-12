#pragma once
#include <QAbstractTableModel>
#include <QByteArray>
#include <cstddef>
#include <vector>
namespace choscordb {
// One bounded byte window. Text row boundaries preserve complete UTF-8 characters.
class ValuePreviewModel final : public QAbstractTableModel {
  public:
    static constexpr qsizetype MaxChunkBytes = 64 * 1024;
    explicit ValuePreviewModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}
    // Text windows preserve leading continuation bytes as invalid UTF-8. A trailing
    // incomplete codepoint before EOF is omitted; nextOffset() points to its lead
    // byte so the next request overlaps it. At EOF, malformed UTF-8 is displayed
    // as explicitly marked hexadecimal. Controls and backslashes are escaped
    // for single-line display. Raw bytes remain unchanged.
    bool setChunk(QByteArray bytes, quint64 offset, quint64 totalBytes, bool binary);
    void clear();
    quint64 nextOffset() const { return nextOffset_; }
    // Owned buffer capacities including the byte terminator; fixed QObject/model
    // storage and transient visible-cell QVariant allocations are excluded.
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
} // namespace choscordb
