#include "models/result_table_model.h"
#include <QArrayData>
#include <QFont>
#include <algorithm>
#include <limits>
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
    if (role == Qt::FontRole && std::holds_alternative<std::monostate>(cell)) {
        QFont font;
        font.setItalic(true);
        return font;
    }
    if (role == Qt::TextAlignmentRole &&
        (std::holds_alternative<qint64>(cell) || std::holds_alternative<double>(cell)))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::UserRole)
        return std::holds_alternative<std::monostate>(cell);
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
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
    beginResetModel();
    // Destroy the previous page before adopting the validated transfer.
    std::vector<ResultColumn>().swap(columns_);
    std::vector<Row>().swap(rows_);
    columns_ = std::move(columns);
    rows_ = std::move(rows);
    firstRow_ = firstRow;
    residentBytes_ = count.bytes;
    endResetModel();
    return true;
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
