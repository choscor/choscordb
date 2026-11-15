#include "models/result_table_model.h"
#include "models/result_table_model_p.h"
#include <limits>
#include <unordered_set>

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
QString ResultTableModel::stageCells(std::vector<StagedCell> cells) {
    result_model_detail::AllocationCounter needed{std::numeric_limits<std::size_t>::max()};
    std::unordered_set<quint64> seen;
    std::size_t released = 0;
    for (const auto& cell : cells) {
        if (!cellWritable(cell.row, cell.column))
            return tr("Row %1, column %2 cannot be edited.")
                .arg(cell.row + 1)
                .arg(headerData(cell.column, Qt::Horizontal, HeaderNameRole).toString());
        if (seen.insert(cellKey(cell.row, cell.column)).second)
            released += editBytes(cell.row, cell.column);
        const auto* bytes = std::get_if<QByteArray>(&cell.value);
        needed.add(editCharge(cell.value, cellDisplay(cell.value)));
        if (bytes)
            needed.binary(*bytes);
    }
    const auto budgetError = tr("These values exceed the remaining edit memory budget. Change "
                                "fewer cells or discard other staged edits.");
    if (needed.bytes > byteBudget_ - residentBytes_ - stagedBytes_ + released)
        return budgetError;
    if (cells.empty())
        return {};
    int top = rowCount(), bottom = -1, left = columnCount(), right = -1;
    QString error;
    for (auto& cell : cells) {
        if (!stageCellAt(cell.row, cell.column, std::move(cell.value), {})) {
            error = budgetError;
            break;
        }
        top = std::min(top, cell.row);
        bottom = std::max(bottom, cell.row);
        left = std::min(left, cell.column);
        right = std::max(right, cell.column);
    }
    if (bottom >= 0) {
        emit dataChanged(index(top, left), index(bottom, right));
        emit pendingEditsChanged(hasPendingEdits());
    }
    return error;
}
}
