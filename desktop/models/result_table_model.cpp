#include "models/result_table_model.h"
#include <QArrayData>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <algorithm>
#include <limits>
#include <numeric>
namespace choscordb {
std::optional<DeferredValue> ResultTableModel::deferredValue(const QModelIndex& index) const {
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.column() < 0 ||
        static_cast<size_t>(index.row()) >= rows_.size() ||
        static_cast<size_t>(index.column()) >= rows_[index.row()].size())
        return std::nullopt;
    if (const auto* value = std::get_if<DeferredValue>(&rows_[index.row()][index.column()]))
        return *value;
    return std::nullopt;
}

int ResultTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}
int ResultTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(columns_.size());
}
QVariant ResultTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.column() < 0 ||
        index.row() >= rowCount() || index.column() >= columnCount())
        return {};
    const auto& cell = rows_[index.row()][index.column()];
    if (role == Qt::BackgroundRole && deleted_[index.row()])
        return QBrush(QColor(255, 215, 215));
    if (role == Qt::BackgroundRole && inserted_[index.row()])
        return QBrush(QColor(220, 245, 220));
    if (role == Qt::BackgroundRole && touched_[index.row()][index.column()])
        return QBrush(QColor(255, 245, 195));
    if (role == Qt::ToolTipRole && deleted_[index.row()])
        return tr("Pending deletion");
    if (role == Qt::ToolTipRole && inserted_[index.row()])
        return touched_[index.row()][index.column()] ? tr("Pending insert value")
                                                     : tr("Omitted; database default applies");
    if (role == Qt::FontRole && std::holds_alternative<std::monostate>(cell)) {
        QFont font;
        font.setItalic(true);
        return font;
    }
    if (role == Qt::TextAlignmentRole &&
        (std::holds_alternative<qint64>(cell) || std::holds_alternative<double>(cell)))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::UserRole)
        return touched_[index.row()][index.column()] &&
               std::holds_alternative<std::monostate>(cell);
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    if (inserted_[index.row()] && !touched_[index.row()][index.column()])
        return QString{};
    return std::visit(
        [](const auto& value) -> QVariant {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return QString("NULL");
            else if constexpr (std::is_same_v<T, bool>)
                return value ? QString("true") : QString("false");
            else if constexpr (std::is_same_v<T, qint64>)
                return QString::number(value);
            else if constexpr (std::is_same_v<T, double>)
                return QString::number(value, 'g', 17);
            else if constexpr (std::is_same_v<T, QString>)
                return value;
            else if constexpr (std::is_same_v<T, QByteArray>)
                return QString("0x") + QString::fromLatin1(value.left(64).toHex()) +
                       (value.size() > 64 ? QString("… (%1 bytes)").arg(value.size()) : QString());
            else
                return QString("[%1 · %2 bytes · open to load]").arg(value.type).arg(value.bytes);
        },
        cell);
}
QVariant ResultTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (section < 0)
        return {};
    if (orientation == Qt::Vertical) {
        if (role != Qt::DisplayRole)
            return {};
        return section < rowCount()
                   ? QVariant::fromValue(firstRow_ + static_cast<quint64>(section) + 1)
                   : QVariant();
    }
    if (section >= columnCount())
        return {};
    const auto& column = columns_[section];
    if (role == Qt::DisplayRole) {
        QString type = column.databaseType;
        if (column.precision && !type.contains('(')) {
            type += '(' + QString::number(*column.precision);
            if (column.scale)
                type += ',' + QString::number(*column.scale);
            type += ')';
        }
        return type.isEmpty() ? column.name : column.name + " · " + type;
    }
    if (role != Qt::ToolTipRole && role != Qt::AccessibleDescriptionRole)
        return {};
    const auto known = [](const auto& value) {
        return value ? QString::number(*value) : QStringLiteral("Unknown");
    };
    const auto nullable = !column.nullable   ? tr("Unknown")
                          : *column.nullable ? tr("Nullable")
                                             : tr("Not nullable");
    return tr("Name: %1\nDatabase type: %2\nPrecision: %3\nScale: %4\nTimezone: "
              "%5\nNullability: %6")
        .arg(column.name, column.databaseType.isEmpty() ? tr("Unknown") : column.databaseType,
             known(column.precision), known(column.scale),
             column.timezone.isEmpty() ? tr("Unknown") : column.timezone, nullable);
}
namespace {
// Qt's refcount/capacity header and conservative alignment padding are owned
// allocations too. Allocator bookkeeping itself is outside the page budget.
constexpr std::size_t QtHeaderBytes = sizeof(QArrayData) + alignof(std::max_align_t);
struct AllocationCounter {
    std::size_t limit;
    std::size_t bytes = 0;
    bool add(std::size_t count, std::size_t width = 1) {
        if (count > (limit - bytes) / width)
            return false;
        bytes += count * width;
        return true;
    }
    bool string(const QString& text) {
        return text.isNull() || (add(QtHeaderBytes) &&
                                 add(std::max(text.capacity(), text.size()) + 1, sizeof(QChar)));
    }
    bool binary(const QByteArray& value) {
        return value.isNull() ||
               (add(QtHeaderBytes) && add(std::max(value.capacity(), value.size()) + 1));
    }
};
} // namespace
bool ResultTableModel::setPage(std::vector<ResultColumn> columns, std::vector<Row> rows,
                               quint64 firstRow) {
    if (columns.size() > std::numeric_limits<int>::max() || rows.size() > 10000 ||
        firstRow > std::numeric_limits<quint64>::max() - rows.size())
        return false;
    if (std::any_of(rows.begin(), rows.end(),
                    [&](const auto& row) { return row.size() != columns.size(); }))
        return false;
    AllocationCounter count{byteBudget_};
    if (!count.add(columns.capacity(), sizeof(ResultColumn)) ||
        !count.add(rows.capacity(), sizeof(Row)))
        return false;
    for (const auto& column : columns)
        if (!count.string(column.name) || !count.string(column.databaseType) ||
            !count.string(column.timezone))
            return false;
    for (const auto& row : rows) {
        if (!count.add(row.capacity(), sizeof(Cell)))
            return false;
        for (const auto& cell : row) {
            const bool fits = std::visit(
                [&count](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, QString>)
                        return count.string(value);
                    else if constexpr (std::is_same_v<T, QByteArray>)
                        return count.binary(value);
                    else if constexpr (std::is_same_v<T, DeferredValue>)
                        return count.string(value.type);
                    else
                        return true;
                },
                cell);
            if (!fits)
                return false;
        }
    }
    // The immutable originals and staging flags are owned beside the visible page.
    // Qt strings and byte arrays remain shared, but the cell vectors are copied.
    if (!count.add(rows.size(), sizeof(Row)) ||
        !count.add(rows.size(), sizeof(std::vector<bool>) + sizeof(std::vector<std::size_t>)) ||
        !count.add(rows.size() * 2 + columns.size() * 2) ||
        !count.add(rows.size(), (columns.size() + 7) / 8) ||
        !count.add(rows.size(), columns.size() * sizeof(std::size_t)))
        return false;
    for (const auto& row : rows)
        if (!count.add(row.size(), sizeof(Cell)))
            return false;
    beginResetModel();
    // Destroy the previous page before adopting the validated transfer.
    std::vector<ResultColumn>().swap(columns_);
    std::vector<Row>().swap(rows_);
    columns_ = std::move(columns);
    rows_ = std::move(rows);
    originalRows_ = rows_;
    touched_.assign(rows_.size(), std::vector<bool>(columns_.size(), false));
    editBytes_.assign(rows_.size(), std::vector<std::size_t>(columns_.size(), 0));
    inserted_.assign(rows_.size(), false);
    deleted_.assign(rows_.size(), false);
    editable_.assign(columns_.size(), false);
    insertEditable_.assign(columns_.size(), false);
    canInsert_ = canDelete_ = false;
    firstRow_ = firstRow;
    residentBytes_ = count.bytes;
    stagedBytes_ = 0;
    endResetModel();
    emit pendingEditsChanged(false);
    return true;
}
void ResultTableModel::setEditableColumns(std::vector<bool> editable, bool canInsert,
                                          bool canDelete, std::vector<bool> insertEditable) {
    if (editable.size() != columns_.size())
        return;
    editable_ = std::move(editable);
    insertEditable_ = insertEditable.size() == columns_.size()
                          ? std::move(insertEditable)
                          : std::vector<bool>(columns_.size(), canInsert);
    canInsert_ = canInsert;
    canDelete_ = canDelete;
    if (!rows_.empty() && !columns_.empty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}
Qt::ItemFlags ResultTableModel::flags(const QModelIndex& index) const {
    auto result = QAbstractTableModel::flags(index);
    if (index.isValid() && index.row() < rowCount() && index.column() < columnCount() &&
        !deleted_[index.row()] &&
        (inserted_[index.row()] ? insertEditable_[index.column()] : editable_[index.column()]) &&
        !std::holds_alternative<QByteArray>(rows_[index.row()][index.column()]) &&
        !std::holds_alternative<DeferredValue>(rows_[index.row()][index.column()]))
        result |= Qt::ItemIsEditable;
    return result;
}
bool ResultTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::EditRole || !(flags(index) & Qt::ItemIsEditable))
        return false;
    const QString text = value.toString();
    if (text == data(index, Qt::EditRole).toString())
        return true;
    const auto bytes = std::size_t(text.size()) * sizeof(QChar) + sizeof(Cell) + QtHeaderBytes;
    const auto oldBytes = editBytes_[index.row()][index.column()];
    if (bytes > byteBudget_ - residentBytes_ - (stagedBytes_ - oldBytes))
        return false;
    const auto type = columns_[index.column()].databaseType.toLower();
    Cell converted = text;
    if (type == "integer" || type == "int" || type == "bigint" || type == "smallint" ||
        type == "int2" || type == "int4" || type == "int8") {
        bool valid = false;
        const auto integer = text.toLongLong(&valid);
        if (!valid)
            return false;
        converted = static_cast<qint64>(integer);
    } else if (type == "boolean" || type == "bool") {
        if (text.compare("true", Qt::CaseInsensitive) == 0 || text == "1")
            converted = true;
        else if (text.compare("false", Qt::CaseInsensitive) == 0 || text == "0")
            converted = false;
        else
            return false;
    } else if (type == "real" || type == "double precision" || type == "float4" ||
               type == "float8") {
        bool valid = false;
        const auto number = text.toDouble(&valid);
        if (!valid)
            return false;
        converted = number;
    }
    rows_[index.row()][index.column()] = std::move(converted);
    touched_[index.row()][index.column()] = true;
    stagedBytes_ = stagedBytes_ - oldBytes + bytes;
    editBytes_[index.row()][index.column()] = bytes;
    emit dataChanged(index, index);
    emit pendingEditsChanged(hasPendingEdits());
    return true;
}
bool ResultTableModel::setNull(const QModelIndex& index) {
    if (!(flags(index) & Qt::ItemIsEditable))
        return false;
    stagedBytes_ -= editBytes_[index.row()][index.column()];
    editBytes_[index.row()][index.column()] = 0;
    rows_[index.row()][index.column()] = std::monostate{};
    touched_[index.row()][index.column()] = true;
    emit dataChanged(index, index);
    emit pendingEditsChanged(hasPendingEdits());
    return true;
}
bool ResultTableModel::addRow() {
    const auto bytes = columns_.size() * (sizeof(Cell) + sizeof(std::size_t)) + sizeof(Row) +
                       sizeof(std::vector<bool>) + sizeof(std::vector<std::size_t>) +
                       (columns_.size() + 7) / 8 + 2;
    if (!canInsert_ || rows_.size() >= 10000 || bytes > byteBudget_ - residentBytes_ - stagedBytes_)
        return false;
    beginInsertRows({}, rowCount(), rowCount());
    rows_.emplace_back(columns_.size());
    touched_.emplace_back(columns_.size(), false);
    editBytes_.emplace_back(columns_.size(), 0);
    inserted_.push_back(true);
    deleted_.push_back(false);
    stagedBytes_ += bytes;
    endInsertRows();
    emit pendingEditsChanged(true);
    return true;
}
void ResultTableModel::markDeleted(const QModelIndexList& selection, bool deleted) {
    std::vector<int> selectedRows;
    for (const auto& i : selection) {
        if (i.isValid() && i.model() == this && i.row() < rowCount())
            selectedRows.push_back(i.row());
    }
    std::sort(selectedRows.begin(), selectedRows.end(), std::greater<int>());
    selectedRows.erase(std::unique(selectedRows.begin(), selectedRows.end()), selectedRows.end());
    for (const int row : selectedRows) {
        if (inserted_[row]) {
            if (!deleted)
                continue;
            const auto bytes = columns_.size() * (sizeof(Cell) + sizeof(std::size_t)) +
                               sizeof(Row) + sizeof(std::vector<bool>) +
                               sizeof(std::vector<std::size_t>) + (columns_.size() + 7) / 8 + 2;
            beginRemoveRows({}, row, row);
            stagedBytes_ -= bytes + std::accumulate(editBytes_[row].begin(), editBytes_[row].end(),
                                                    std::size_t{0});
            rows_.erase(rows_.begin() + row);
            touched_.erase(touched_.begin() + row);
            editBytes_.erase(editBytes_.begin() + row);
            inserted_.erase(inserted_.begin() + row);
            deleted_.erase(deleted_.begin() + row);
            endRemoveRows();
        } else if (canDelete_) {
            deleted_[row] = deleted;
            if (columnCount())
                emit dataChanged(index(row, 0), index(row, columnCount() - 1));
        }
    }
    emit pendingEditsChanged(hasPendingEdits());
}
void ResultTableModel::discardEdits() {
    beginResetModel();
    rows_ = originalRows_;
    touched_.assign(rows_.size(), std::vector<bool>(columns_.size(), false));
    editBytes_.assign(rows_.size(), std::vector<std::size_t>(columns_.size(), 0));
    inserted_.assign(rows_.size(), false);
    deleted_.assign(rows_.size(), false);
    stagedBytes_ = 0;
    endResetModel();
    emit pendingEditsChanged(false);
}
bool ResultTableModel::hasPendingEdits() const {
    for (size_t r = 0; r < rows_.size(); ++r) {
        if (inserted_[r] || deleted_[r])
            return true;
        for (size_t c = 0; c < columns_.size(); ++c)
            if (touched_[r][c])
                return true;
    }
    return false;
}
QString ResultTableModel::copyRows(QModelIndexList selection, QString* error) const {
    return copyScope(std::move(selection), 1, error);
}
QString ResultTableModel::copyPage(QString* error) const {
    return copyScope({}, 2, error);
}
QString ResultTableModel::copyCells(QModelIndexList selection, QString* error) const {
    return copyScope(std::move(selection), 0, error);
}
QString ResultTableModel::copyScope(QModelIndexList selection, int scope, QString* error) const {
    if (error)
        error->clear();
    auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return QString{};
    };
    if (rows_.empty() || columns_.empty())
        return {};
    selection.erase(std::remove_if(selection.begin(), selection.end(),
                                   [this](const auto& i) {
                                       return !i.isValid() || i.model() != this ||
                                              i.row() >= rowCount() || i.column() >= columnCount();
                                   }),
                    selection.end());
    if (scope != 2 && selection.isEmpty())
        return {};
    std::sort(selection.begin(), selection.end(), [](const auto& a, const auto& b) {
        return a.row() == b.row() ? a.column() < b.column() : a.row() < b.row();
    });
    selection.erase(std::unique(selection.begin(), selection.end()), selection.end());
    std::vector<int> selectedRows;
    if (scope == 1)
        for (const auto& i : selection)
            if (selectedRows.empty() || selectedRows.back() != i.row())
                selectedRows.push_back(i.row());
    int first = scope == 2 ? 0 : selection.front().row();
    int lines = scope == 1 ? int(selectedRows.size())
                           : (scope == 2 ? rowCount() : selection.back().row() - first + 1);
    int minColumn = 0, maxColumn = columnCount() - 1;
    if (scope == 0) {
        minColumn = maxColumn = selection.front().column();
        for (const auto& i : selection) {
            minColumn = std::min(minColumn, i.column());
            maxColumn = std::max(maxColumn, i.column());
        }
    }
    const auto limit =
        std::min<std::size_t>(byteBudget_ / sizeof(QChar), std::numeric_limits<qsizetype>::max());
    if (std::size_t(lines) > limit / std::size_t(maxColumn - minColumn + 1))
        return fail(tr("Copied selection exceeds the clipboard size limit."));
    QString output;
    std::size_t count = 0;
    // Count exact escaped output first, then reserve once. Neither row nor page
    // scope constructs a QModelIndex for every cell in advance.
    for (bool write : {false, true}) {
        auto selected = selection.cbegin();
        auto append = [&](QChar character) {
            if (write)
                output += character;
            else
                ++count;
        };
        for (int line = 0; line < lines; ++line) {
            const int row = scope == 1 ? selectedRows[line] : first + line;
            if (line)
                append('\n');
            for (int column = minColumn; column <= maxColumn; ++column) {
                if (column != minColumn)
                    append('\t');
                if (count > limit)
                    return fail(tr("Copied selection exceeds the clipboard size limit."));
                if (scope == 0 && (selected == selection.cend() || selected->row() != row ||
                                   selected->column() != column))
                    continue;
                if (scope == 0)
                    ++selected;
                const auto& value = rows_[row][column];
                if (std::holds_alternative<DeferredValue>(value))
                    return fail(tr("Large values cannot be copied from the grid. Export the "
                                   "result to copy the complete value."));
                if (const auto* binary = std::get_if<QByteArray>(&value)) {
                    if (limit < 2 || std::size_t(binary->size()) > (limit - 2) / 2)
                        return fail(tr("Copied selection exceeds the clipboard size limit."));
                    const auto length = 2 + std::size_t(binary->size()) * 2;
                    if (!write) {
                        if (length > limit - count)
                            return fail(tr("Copied selection exceeds the clipboard size limit."));
                        count += length;
                    } else
                        output += QStringLiteral("0x") + QString::fromLatin1(binary->toHex());
                    continue;
                }
                const auto text = data(index(row, column)).toString();
                const bool quote = text.contains('\t') || text.contains('\n') ||
                                   text.contains('\r') || text.contains('"');
                if (!write) {
                    const auto length =
                        std::size_t(text.size()) + std::size_t(text.count('"')) + (quote ? 2 : 0);
                    if (length > limit - count)
                        return fail(tr("Copied selection exceeds the clipboard size limit."));
                    count += length;
                } else {
                    if (quote)
                        output += '"';
                    for (const auto character : text) {
                        if (character == '"')
                            output += '"';
                        output += character;
                    }
                    if (quote)
                        output += '"';
                }
            }
        }
        if (!write) {
            if (count > limit)
                return fail(tr("Copied selection exceeds the clipboard size limit."));
            output.reserve(qsizetype(count));
        }
    }
    return output;
}
} // namespace choscordb
