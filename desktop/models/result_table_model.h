#pragma once
#include <QAbstractTableModel>
#include <QByteArray>
#include <QString>
#include <cstddef>
#include <optional>
#include <variant>
#include <vector>
namespace choscordb {
struct DeferredValue {
    quint64 handle;
    quint64 bytes;
    QString type;
};
using Cell = std::variant<std::monostate, bool, qint64, double, QString, QByteArray, DeferredValue>;
struct ResultColumn {
    QString name;
    QString databaseType;
    std::optional<quint32> precision;
    std::optional<qint32> scale;
    QString timezone;
    std::optional<bool> nullable;
};
class ResultTableModel final : public QAbstractTableModel {
    Q_OBJECT
  public:
    using Row = std::vector<Cell>;
    static constexpr std::size_t DefaultBytes = 64 * 1024 * 1024;
    // Budget includes owned page allocations, excluding this fixed QObject and
    // caller-owned in-flight transfers. Shared Qt buffers are charged in full.
    explicit ResultTableModel(QObject* parent = nullptr, std::size_t byteBudget = DefaultBytes)
        : QAbstractTableModel(parent), byteBudget_(byteBudget) {}
    std::size_t residentBytes() const { return residentBytes_; }
    std::size_t byteBudget() const { return byteBudget_; }
    bool setByteBudget(std::size_t bytes) {
        if (bytes < residentBytes_)
            return false;
        byteBudget_ = bytes;
        return true;
    }
    std::optional<DeferredValue> deferredValue(const QModelIndex& index) const;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    bool setPage(std::vector<ResultColumn> columns, std::vector<Row> rows, quint64 firstRow);
    // Copies a rectangle covering the selection, leaving unselected cells blank.
    // Deferred values must be loaded before copying; failures return empty text.
    QString copyCells(QModelIndexList selection, QString* error = nullptr) const;
    QString copyRows(QModelIndexList selection, QString* error = nullptr) const;
    QString copyPage(QString* error = nullptr) const;

  private:
    QString copyScope(QModelIndexList selection, int scope, QString* error) const;
    std::vector<ResultColumn> columns_;
    std::vector<Row> rows_;
    quint64 firstRow_ = 0;
    std::size_t byteBudget_;
    std::size_t residentBytes_ = 0;
};
} // namespace choscordb
