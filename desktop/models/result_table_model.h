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
        if (bytes < residentBytes_ + stagedBytes_)
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
    void setEditableColumns(std::vector<bool> editable, bool canInsert, bool canDelete,
                            std::vector<bool> insertEditable = {});
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    bool setNull(const QModelIndex& index);
    bool addRow();
    void markDeleted(const QModelIndexList& selection, bool deleted);
    void discardEdits();
    bool hasPendingEdits() const;
    bool canInsert() const { return canInsert_; }
    bool canDelete() const { return canDelete_; }
    const std::vector<Row>& originalRows() const { return originalRows_; }
    const std::vector<Row>& rows() const { return rows_; }
    const std::vector<std::vector<bool>>& touched() const { return touched_; }
    const std::vector<bool>& inserted() const { return inserted_; }
    const std::vector<bool>& deleted() const { return deleted_; }
    // Copies a rectangle covering the selection, leaving unselected cells blank.
    // Deferred values must be loaded before copying; failures return empty text.
    QString copyCells(QModelIndexList selection, QString* error = nullptr) const;
    QString copyRows(QModelIndexList selection, QString* error = nullptr) const;
    QString copyPage(QString* error = nullptr) const;

  signals:
    void pendingEditsChanged(bool pending);

  private:
    QString copyScope(QModelIndexList selection, int scope, QString* error) const;
    std::vector<ResultColumn> columns_;
    std::vector<Row> rows_;
    std::vector<Row> originalRows_;
    std::vector<std::vector<bool>> touched_;
    std::vector<std::vector<std::size_t>> editBytes_;
    std::vector<bool> inserted_, deleted_, editable_;
    std::vector<bool> insertEditable_;
    bool canInsert_ = false, canDelete_ = false;
    quint64 firstRow_ = 0;
    std::size_t byteBudget_;
    std::size_t residentBytes_ = 0;
    std::size_t stagedBytes_ = 0;
};
} // namespace choscordb
