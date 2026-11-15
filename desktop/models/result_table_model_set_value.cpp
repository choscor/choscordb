#include "models/result_table_model.h"

namespace choscordb {
std::vector<QString> ResultTableModel::expressionRow(std::size_t row) const {
    std::vector<QString> result(columns_.size());
    if (!edits_.empty())
        for (std::size_t column = 0; column < result.size(); ++column)
            if (const auto* value = edit(row, column))
                result[column] = value->expression;
    return result;
}
bool ResultTableModel::cellWritable(int row, int column) const {
    return row >= 0 && column >= 0 && row < rowCount() && column < columnCount() &&
           !deleted_[row] && (inserted_[row] ? insertEditable_[column] : editable_[column]);
}
std::optional<bool> ResultTableModel::columnNullable(int column) const {
    if (column >= 0 && column < static_cast<int>(cellMetadata_.size()) &&
        cellMetadata_[column].nullable)
        return cellMetadata_[column].nullable;
    if (column >= 0 && column < columnCount())
        return columns_[column].nullable;
    return std::nullopt;
}
bool ResultTableModel::stageCell(const QModelIndex& index, Cell value, const QString& expression) {
    if (index.model() != this || !cellWritable(index.row(), index.column()) ||
        !stageCellAt(index.row(), index.column(), std::move(value), expression))
        return false;
    emit dataChanged(index, index);
    emit pendingEditsChanged(hasPendingEdits());
    return true;
}
bool ResultTableModel::omitInsertedCell(const QModelIndex& index) {
    if (index.model() != this || !cellWritable(index.row(), index.column()) ||
        !inserted_[index.row()])
        return false;
    if (const auto found = edits_.find(cellKey(index.row(), index.column()));
        found != edits_.end()) {
        stagedBytes_ -= found->second.bytes;
        edits_.erase(found);
    }
    rows_[index.row()][index.column()] = Cell{};
    emit dataChanged(index, index);
    emit pendingEditsChanged(hasPendingEdits());
    return true;
}
}
