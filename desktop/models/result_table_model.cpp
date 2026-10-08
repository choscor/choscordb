#include "models/result_table_model.h"
#include "bridge/engine_adapter.h"
#include "design_system/table/table_style.h"
#include <QArrayData>
#include <QFont>
#include <QRect>
#include <algorithm>
#include <limits>
#include <new>
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
std::optional<Cell> ResultTableModel::cellValue(const QModelIndex& index) const {
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.column() < 0 ||
        index.row() >= rowCount() || index.column() >= columnCount())
        return std::nullopt;
    return rows_[index.row()][index.column()];
}

const ResultTableModel::Row& ResultTableModel::originalRow(std::size_t row) const {
    if (const auto found = editedOriginals_.find(row); found != editedOriginals_.end())
        return found->second;
    return rows_[row];
}

bool ResultTableModel::linkable(int row, int column) const {
    if (row < 0 || column < 0 || row >= rowCount() || column >= columnCount() ||
        column >= static_cast<int>(linkSlots_.size()) || linkSlots_[column] < 0 ||
        (inserted_[row] && !touched_[row][column]))
        return false;
    const auto slot = static_cast<std::size_t>(row) * static_cast<std::size_t>(linkColumnCount_) +
                      static_cast<std::size_t>(linkSlots_[column]);
    if (slot >= linkCache_.size())
        linkCache_.resize(rows_.size() * static_cast<std::size_t>(linkColumnCount_), 0);
    auto& cached = linkCache_[slot];
    // The value policy is evaluated once per cell; edits and page changes reset the slot.
    if (cached == 0)
        cached = EngineAdapter::foreignKeyValueFilterable(rows_[row][column]) ? 2 : 1;
    return cached == 2;
}

void ResultTableModel::resetLinkCache() {
    std::vector<quint8>().swap(linkCache_);
}

void ResultTableModel::resetPendingCounts() {
    editedOriginals_.clear();
    originalRowCount_ = rows_.size();
    touchedCount_ = insertedCount_ = deletedCount_ = 0;
}

std::optional<ResultCellMetadata> ResultTableModel::linkedColumn(const QModelIndex& index) const {
    if (!index.isValid() || index.model() != this || !linkable(index.row(), index.column()))
        return std::nullopt;
    return cellMetadata_[index.column()];
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
    // The delegate maps staged-change state to theme surfaces.
    if (role == design::CellChangeRole)
        return deleted_[index.row()]                   ? int(design::CellChange::Deleted)
               : inserted_[index.row()]                ? int(design::CellChange::Inserted)
               : touched_[index.row()][index.column()] ? int(design::CellChange::Changed)
                                                       : int(design::CellChange::None);
    if (role == design::ForeignKeyLinkRole)
        return linkable(index.row(), index.column());
    if (role == Qt::ToolTipRole && deleted_[index.row()])
        return tr("Pending deletion");
    if (role == Qt::ToolTipRole && inserted_[index.row()])
        return touched_[index.row()][index.column()] ? tr("Pending insert value")
                                                     : tr("Omitted; database default applies");
    if (role == ResultValueKindRole) {
        if (std::holds_alternative<FallbackText>(cell))
            return QStringLiteral("fallback_text");
        if (std::holds_alternative<UnavailableValue>(cell))
            return QStringLiteral("unavailable");
        if (const auto* deferred = std::get_if<DeferredValue>(&cell);
            deferred && deferred->fallback)
            return QStringLiteral("deferred_fallback");
    }
    if (role == Qt::ToolTipRole) {
        if (const auto* fallback = std::get_if<FallbackText>(&cell))
            return tr("Read-only server text fallback · %1").arg(fallback->databaseType);
        if (const auto* unavailable = std::get_if<UnavailableValue>(&cell))
            return tr("Unavailable · %1: %2").arg(unavailable->databaseType, unavailable->reason);
        if (const auto* deferred = std::get_if<DeferredValue>(&cell);
            deferred && deferred->fallback)
            return tr("Read-only server text fallback · %1 · open to load").arg(deferred->type);
    }
    if (role == Qt::FontRole && std::holds_alternative<std::monostate>(cell)) {
        static const QFont italic = [] {
            QFont font;
            font.setItalic(true);
            return font;
        }();
        return italic;
    }
    if (role == Qt::TextAlignmentRole &&
        (std::holds_alternative<qint64>(cell) || std::holds_alternative<double>(cell) ||
         std::holds_alternative<DecimalValue>(cell)))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::UserRole)
        return touched_[index.row()][index.column()] &&
               std::holds_alternative<std::monostate>(cell);
    if (role == design::CellNullRole)
        return std::holds_alternative<std::monostate>(cell);
    if (role == design::ChoiceLabelsRole && flags(index) & Qt::ItemIsEditable &&
        index.column() < static_cast<int>(cellMetadata_.size())) {
        const auto& metadata = cellMetadata_[index.column()];
        if (metadata.boolean)
            return QStringList{QStringLiteral("true"), QStringLiteral("false")};
        return metadata.enumChoices;
    }
    if (role == design::ChoiceNullableRole &&
        index.column() < static_cast<int>(cellMetadata_.size()))
        return cellMetadata_[index.column()].nullable == true;
    if (role == design::ForeignKeyLinkLabelRole) {
        if (const auto linked = linkedColumn(index))
            return tr("Open referenced row in %1 (%2)")
                .arg(linked->targetQualifiedName, linked->targetColumn);
        return {};
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    if (inserted_[index.row()] && !touched_[index.row()][index.column()])
        return QString{};
    if (role == Qt::DisplayRole)
        if (const auto* value = std::get_if<QString>(&cell)) {
            // Long or multi-line text is shown as a bounded single-line preview.
            const auto isBreak = [](QChar character) {
                return character == QLatin1Char('\n') || character == QLatin1Char('\r') ||
                       character == QChar::LineSeparator || character == QChar::ParagraphSeparator;
            };
            const auto limit = std::min(value->size(), DisplayPreviewChars);
            const auto begin = value->cbegin();
            if (value->size() <= DisplayPreviewChars && std::none_of(begin, begin + limit, isBreak))
                return *value;
            QString preview = value->left(limit);
            for (auto& character : preview)
                if (isBreak(character))
                    character = QLatin1Char(' ');
            if (value->size() > DisplayPreviewChars)
                preview += QChar(0x2026);
            return preview;
        }
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
            else if constexpr (std::is_same_v<T, DecimalValue>)
                return value.text;
            else if constexpr (std::is_same_v<T, QString>)
                return value;
            else if constexpr (std::is_same_v<T, QByteArray>)
                return QString("0x") + QString::fromLatin1(value.left(64).toHex()) +
                       (value.size() > 64 ? QString("… (%1 bytes)").arg(value.size()) : QString());
            else if constexpr (std::is_same_v<T, DeferredValue>)
                return value.fallback ? tr("[text fallback · %1 · %2 bytes · open to load]")
                                            .arg(value.type)
                                            .arg(value.bytes)
                                      : QString("[%1 · %2 bytes · open to load]")
                                            .arg(value.type)
                                            .arg(value.bytes);
            else if constexpr (std::is_same_v<T, FallbackText>) {
                constexpr qsizetype PreviewChars = 128;
                const auto preview = value.text.left(PreviewChars);
                return tr("[text fallback · %1] %2%3")
                    .arg(value.databaseType, preview,
                         value.text.size() > PreviewChars ? QStringLiteral("…") : QString{});
            } else if constexpr (std::is_same_v<T, UnavailableValue>)
                return tr("[unavailable · %1]").arg(value.databaseType);
            else
                return QString{};
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
    if (role == HeaderNameRole)
        return column.name;
    if (role == HeaderKeyRole)
        return section < static_cast<int>(keyColumns_.size()) && keyColumns_[section];
    if (role == HeaderTypeRole) {
        QString type = column.databaseType;
        if (column.precision && !type.contains('(')) {
            type += '(' + QString::number(*column.precision);
            if (column.scale)
                type += ',' + QString::number(*column.scale);
            type += ')';
        }
        return type;
    }
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
        if (width == 0)
            return true;
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
                    else if constexpr (std::is_same_v<T, DecimalValue>)
                        return count.string(value.text);
                    else if constexpr (std::is_same_v<T, QByteArray>)
                        return count.binary(value);
                    else if constexpr (std::is_same_v<T, DeferredValue>)
                        return count.string(value.type);
                    else if constexpr (std::is_same_v<T, FallbackText>)
                        return count.string(value.text) && count.string(value.databaseType);
                    else if constexpr (std::is_same_v<T, UnavailableValue>)
                        return count.string(value.databaseType) && count.string(value.reason);
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
        !count.add(rows.size() * 2 + columns.size() * 2) || !count.add((columns.size() + 7) / 8) ||
        !count.add(rows.size(), (columns.size() + 7) / 8) ||
        !count.add(rows.size(), columns.size() * sizeof(std::size_t)))
        return false;
    for (const auto& row : rows)
        if (!count.add(row.size(), sizeof(Cell)))
            return false;
    beginResetModel();
    // Destroy the previous page before adopting the validated transfer.
    std::vector<ResultColumn>().swap(columns_);
    std::vector<ResultCellMetadata>().swap(cellMetadata_);
    std::vector<Row>().swap(rows_);
    columns_ = std::move(columns);
    rows_ = std::move(rows);
    resetPendingCounts();
    linkSlots_.assign(columns_.size(), -1);
    linkColumnCount_ = 0;
    resetLinkCache();
    touched_.assign(rows_.size(), std::vector<bool>(columns_.size(), false));
    editBytes_.assign(rows_.size(), std::vector<std::size_t>(columns_.size(), 0));
    inserted_.assign(rows_.size(), false);
    deleted_.assign(rows_.size(), false);
    editable_.assign(columns_.size(), false);
    insertEditable_.assign(columns_.size(), false);
    keyColumns_.assign(columns_.size(), false);
    canInsert_ = canDelete_ = false;
    firstRow_ = firstRow;
    residentBytes_ = count.bytes;
    stagedBytes_ = 0;
    endResetModel();
    emit pendingEditsChanged(false);
    return true;
}
bool ResultTableModel::setCellMetadata(std::vector<ResultCellMetadata> metadata) {
    if (metadata.size() != columns_.size())
        return false;
    // Catalog labels are bounded independently of a page transfer's allocation.
    std::size_t bytes = metadata.capacity() * sizeof(ResultCellMetadata);
    for (const auto& item : metadata) {
        for (const auto& string : {item.sourceColumn, item.sourceObject, item.sourceQualifiedName,
                                   item.targetObject, item.targetQualifiedName, item.targetColumn})
            bytes += static_cast<std::size_t>(string.size()) * sizeof(QChar);
        for (const auto& choice : item.enumChoices)
            bytes += static_cast<std::size_t>(choice.size()) * sizeof(QChar);
    }
    if (bytes > 1024 * 1024)
        return false;
    cellMetadata_ = std::move(metadata);
    linkSlots_.assign(columns_.size(), -1);
    linkColumnCount_ = 0;
    for (std::size_t column = 0; column < cellMetadata_.size(); ++column) {
        const auto& item = cellMetadata_[column];
        if (!item.sourceColumn.isEmpty() && !item.sourceObject.isEmpty() &&
            !item.targetObject.isEmpty() && !item.targetQualifiedName.isEmpty() &&
            !item.targetColumn.isEmpty())
            linkSlots_[column] = linkColumnCount_++;
    }
    resetLinkCache();
    if (rowCount() && columnCount())
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                         {design::ChoiceLabelsRole, design::ChoiceNullableRole,
                          design::ForeignKeyLinkLabelRole, design::ForeignKeyLinkRole});
    return true;
}
void ResultTableModel::setKeyColumns(std::vector<bool> keys) {
    if (keys.size() != columns_.size())
        return;
    keyColumns_ = std::move(keys);
    if (!keyColumns_.empty())
        emit headerDataChanged(Qt::Horizontal, 0, static_cast<int>(keyColumns_.size()) - 1);
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
bool ResultTableModel::cellEditable(int row, int column) const {
    if (row < 0 || column < 0 || row >= rowCount() || column >= columnCount() || deleted_[row] ||
        !(inserted_[row] ? insertEditable_[column] : editable_[column]))
        return false;
    const auto& cell = rows_[row][column];
    return !std::holds_alternative<QByteArray>(cell) &&
           !std::holds_alternative<DeferredValue>(cell) &&
           !std::holds_alternative<FallbackText>(cell) &&
           !std::holds_alternative<UnavailableValue>(cell);
}
Qt::ItemFlags ResultTableModel::flags(const QModelIndex& index) const {
    auto result = QAbstractTableModel::flags(index);
    if (index.isValid() && index.model() == this && cellEditable(index.row(), index.column()))
        result |= Qt::ItemIsEditable;
    return result;
}
void ResultTableModel::preserveOriginal(int row) {
    if (static_cast<std::size_t>(row) < originalRowCount_ && !inserted_[row])
        editedOriginals_.try_emplace(static_cast<std::size_t>(row), rows_[row]);
}
void ResultTableModel::markTouched(int row, int column) {
    if (!touched_[row][column]) {
        touched_[row][column] = true;
        ++touchedCount_;
    }
    if (column < static_cast<int>(linkSlots_.size()) && linkSlots_[column] >= 0) {
        const auto slot =
            static_cast<std::size_t>(row) * static_cast<std::size_t>(linkColumnCount_) +
            static_cast<std::size_t>(linkSlots_[column]);
        if (slot < linkCache_.size())
            linkCache_[slot] = 0;
    }
}
bool ResultTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role == design::TypedNullEditRole) {
        if (index.isValid() && index.column() < static_cast<int>(cellMetadata_.size()) &&
            cellMetadata_[index.column()].nullable == true)
            return setNull(index);
        return false;
    }
    if (role != Qt::EditRole || !(flags(index) & Qt::ItemIsEditable))
        return false;
    const QString text = value.toString();
    if (text == data(index, Qt::EditRole).toString())
        return true;
    const auto bytes = std::size_t(text.size()) * sizeof(QChar) + sizeof(Cell) + QtHeaderBytes;
    const auto oldBytes = editBytes_[index.row()][index.column()];
    if (bytes > byteBudget_ - residentBytes_ - (stagedBytes_ - oldBytes))
        return false;
    auto converted = EngineAdapter::parseGridEditValue(columns_[index.column()].databaseType, text);
    if (!converted)
        return false;
    preserveOriginal(index.row());
    rows_[index.row()][index.column()] = std::move(*converted);
    markTouched(index.row(), index.column());
    stagedBytes_ = stagedBytes_ - oldBytes + bytes;
    editBytes_[index.row()][index.column()] = bytes;
    emit dataChanged(index, index);
    emit pendingEditsChanged(hasPendingEdits());
    return true;
}
ResultTableModel::CellEditSnapshot ResultTableModel::cellEditSnapshot(const QModelIndex& index,
                                                                      const QString& text) const {
    if (index.model() != this || !(flags(index) & Qt::ItemIsEditable))
        return {};
    const auto oldBytes = editBytes_[index.row()][index.column()];
    return {columns_[index.column()].databaseType, text,
            byteBudget_ - residentBytes_ - (stagedBytes_ - oldBytes), true};
}
ResultTableModel::CellEditEvaluation ResultTableModel::evaluateCellEdit(CellEditSnapshot snapshot) {
    CellEditEvaluation result;
    if (!snapshot.eligible) {
        result.error =
            tr("This cell is no longer editable. Close the editor and reopen an eligible cell.");
        return result;
    }
    // Bound text before UTF-8 conversion and backend parsing can allocate copies.
    AllocationCounter preflight{snapshot.byteBudget};
    if (!preflight.add(sizeof(Cell)) || !preflight.add(QtHeaderBytes) ||
        !preflight.add(std::size_t(snapshot.text.size()) + 1, sizeof(QChar))) {
        result.state = CellEditState::ResourceRefused;
        result.error = tr("This value exceeds the remaining edit memory budget. Shorten the value "
                          "or discard other staged edits.");
        return result;
    }
    try {
        result.value =
            EngineAdapter::parseGridEditValue(snapshot.databaseType, snapshot.text, &result.error);
    } catch (const std::bad_alloc&) {
        result.state = CellEditState::ResourceRefused;
        result.error =
            tr("There is not enough memory to edit this value. Shorten the value and try again.");
        return result;
    }
    result.state = result.value ? CellEditState::Ready : CellEditState::TypeRejected;
    result.bytes = preflight.bytes;
    return result;
}
ResultTableModel::CellEditEvaluation
ResultTableModel::stageCellEdit(const QModelIndex& index, CellEditEvaluation evaluation) {
    if (evaluation.state != CellEditState::Ready)
        return evaluation;
    if (!evaluation.value || index.model() != this || !(flags(index) & Qt::ItemIsEditable)) {
        evaluation.state = CellEditState::Ineligible;
        evaluation.error =
            tr("This cell is no longer editable. Close the editor and reopen an eligible cell.");
        return evaluation;
    }
    const auto oldBytes = editBytes_[index.row()][index.column()];
    AllocationCounter charge{byteBudget_ - residentBytes_ - (stagedBytes_ - oldBytes)};
    const bool fits =
        charge.add(sizeof(Cell)) && std::visit(
                                        [&](const auto& value) {
                                            using T = std::decay_t<decltype(value)>;
                                            if constexpr (std::is_same_v<T, QString>)
                                                return charge.string(value);
                                            else if constexpr (std::is_same_v<T, DecimalValue>)
                                                return charge.string(value.text);
                                            else
                                                return true;
                                        },
                                        *evaluation.value);
    if (!fits) {
        evaluation.state = CellEditState::ResourceRefused;
        evaluation.error = tr("This value exceeds the remaining edit memory budget. Shorten the "
                              "value or discard other staged edits.");
        return evaluation;
    }
    evaluation.bytes = charge.bytes;
    preserveOriginal(index.row());
    rows_[index.row()][index.column()] = std::move(*evaluation.value);
    markTouched(index.row(), index.column());
    stagedBytes_ = stagedBytes_ - oldBytes + evaluation.bytes;
    editBytes_[index.row()][index.column()] = evaluation.bytes;
    emit dataChanged(index, index);
    emit pendingEditsChanged(hasPendingEdits());
    return evaluation;
}
void ResultTableModel::stageNull(int row, int column) {
    stagedBytes_ -= editBytes_[row][column];
    editBytes_[row][column] = 0;
    preserveOriginal(row);
    rows_[row][column] = std::monostate{};
    markTouched(row, column);
}
bool ResultTableModel::setNull(const QModelIndex& index) {
    if (!(flags(index) & Qt::ItemIsEditable))
        return false;
    stageNull(index.row(), index.column());
    emit dataChanged(index, index);
    emit pendingEditsChanged(hasPendingEdits());
    return true;
}
bool ResultTableModel::setNull(const QItemSelection& selection) {
    bool changed = false;
    for (const auto& range : selection) {
        if (!range.isValid() || range.model() != this)
            continue;
        const int top = std::max(0, range.top());
        const int bottom = std::min(rowCount() - 1, range.bottom());
        const int left = std::max(0, range.left());
        const int right = std::min(columnCount() - 1, range.right());
        int firstRow = bottom + 1, lastRow = -1, firstColumn = right + 1, lastColumn = -1;
        for (int column = left; column <= right; ++column) {
            if (!editable_[column] && !insertEditable_[column])
                continue;
            for (int row = top; row <= bottom; ++row) {
                if (!cellEditable(row, column))
                    continue;
                stageNull(row, column);
                firstRow = std::min(firstRow, row);
                lastRow = std::max(lastRow, row);
                firstColumn = std::min(firstColumn, column);
                lastColumn = std::max(lastColumn, column);
            }
        }
        if (lastRow < 0)
            continue;
        changed = true;
        emit dataChanged(index(firstRow, firstColumn), index(lastRow, lastColumn));
    }
    if (changed)
        emit pendingEditsChanged(hasPendingEdits());
    return changed;
}
bool ResultTableModel::hasEditableCell(const QItemSelection& selection) const {
    for (const auto& range : selection) {
        if (!range.isValid() || range.model() != this)
            continue;
        const int top = std::max(0, range.top());
        const int bottom = std::min(rowCount() - 1, range.bottom());
        const int right = std::min(columnCount() - 1, range.right());
        for (int column = std::max(0, range.left()); column <= right; ++column) {
            if (!editable_[column] && !insertEditable_[column])
                continue;
            for (int row = top; row <= bottom; ++row)
                if (cellEditable(row, column))
                    return true;
        }
    }
    return false;
}
std::vector<int> ResultTableModel::selectedRows(const QItemSelection& selection) const {
    std::vector<bool> selected(rows_.size(), false);
    for (const auto& range : selection) {
        if (!range.isValid() || range.model() != this || range.left() >= columnCount() ||
            range.right() < 0)
            continue;
        const int bottom = std::min(rowCount() - 1, range.bottom());
        for (int row = std::max(0, range.top()); row <= bottom; ++row)
            selected[row] = true;
    }
    std::vector<int> rows;
    for (std::size_t row = 0; row < selected.size(); ++row)
        if (selected[row])
            rows.push_back(static_cast<int>(row));
    return rows;
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
    ++insertedCount_;
    resetLinkCache();
    stagedBytes_ += bytes;
    endInsertRows();
    emit pendingEditsChanged(true);
    return true;
}
bool ResultTableModel::duplicateRow(int row, QString* error) {
    return duplicateRow(row, insertEditable_, error);
}
bool ResultTableModel::duplicateRow(int row, const std::vector<bool>& copyable, QString* error) {
    if (error)
        error->clear();
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!canInsert_ || row < 0 || row >= rowCount() || copyable.size() != columns_.size())
        return fail(tr("This result cannot insert a duplicate row."));
    if (rows_.size() >= 10000)
        return fail(tr("The visible page already has the maximum number of rows."));
    if (std::any_of(rows_[row].begin(), rows_[row].end(), [](const Cell& value) {
            return std::holds_alternative<FallbackText>(value) ||
                   std::holds_alternative<UnavailableValue>(value);
        }))
        return fail(tr("Rows with fallback or unavailable values cannot be duplicated."));
    Row duplicate(columns_.size());
    std::vector<bool> touched(columns_.size(), false);
    for (size_t column = 0; column < columns_.size(); ++column) {
        if (!insertEditable_[column] || !copyable[column])
            continue;
        if (std::holds_alternative<DeferredValue>(rows_[row][column]))
            return fail(tr("Load a complete value before duplicating this row."));
        duplicate[column] = rows_[row][column];
        touched[column] = true;
    }
    const auto bytes = columns_.size() * (sizeof(Cell) + sizeof(std::size_t)) + sizeof(Row) +
                       sizeof(std::vector<bool>) + sizeof(std::vector<std::size_t>) +
                       (columns_.size() + 7) / 8 + 2;
    if (bytes > byteBudget_ - residentBytes_ - stagedBytes_)
        return fail(tr("Duplicating this row would exceed the result grid memory limit."));
    beginInsertRows({}, rowCount(), rowCount());
    touchedCount_ += static_cast<std::size_t>(std::count(touched.begin(), touched.end(), true));
    rows_.push_back(std::move(duplicate));
    touched_.push_back(std::move(touched));
    editBytes_.emplace_back(columns_.size(), 0);
    inserted_.push_back(true);
    deleted_.push_back(false);
    ++insertedCount_;
    resetLinkCache();
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
    markRowsDeleted(std::move(selectedRows), deleted);
}
void ResultTableModel::markRowsDeleted(std::vector<int> selectedRows, bool deleted) {
    selectedRows.erase(std::remove_if(selectedRows.begin(), selectedRows.end(),
                                      [this](int row) { return row < 0 || row >= rowCount(); }),
                       selectedRows.end());
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
            touchedCount_ -= static_cast<std::size_t>(
                std::count(touched_[row].begin(), touched_[row].end(), true));
            --insertedCount_;
            resetLinkCache();
            rows_.erase(rows_.begin() + row);
            touched_.erase(touched_.begin() + row);
            editBytes_.erase(editBytes_.begin() + row);
            inserted_.erase(inserted_.begin() + row);
            deleted_.erase(deleted_.begin() + row);
            endRemoveRows();
        } else if (canDelete_ &&
                   (!deleted ||
                    std::none_of(rows_[row].begin(), rows_[row].end(), [](const Cell& cell) {
                        return std::holds_alternative<FallbackText>(cell) ||
                               std::holds_alternative<UnavailableValue>(cell);
                    }))) {
            if (deleted_[row] != deleted)
                deleted ? ++deletedCount_ : --deletedCount_;
            deleted_[row] = deleted;
            if (columnCount())
                emit dataChanged(index(row, 0), index(row, columnCount() - 1));
        }
    }
    emit pendingEditsChanged(hasPendingEdits());
}
void ResultTableModel::discardEdits() {
    beginResetModel();
    rows_.resize(originalRowCount_);
    for (auto& [row, original] : editedOriginals_)
        rows_[row] = std::move(original);
    resetPendingCounts();
    resetLinkCache();
    touched_.assign(rows_.size(), std::vector<bool>(columns_.size(), false));
    editBytes_.assign(rows_.size(), std::vector<std::size_t>(columns_.size(), 0));
    inserted_.assign(rows_.size(), false);
    deleted_.assign(rows_.size(), false);
    stagedBytes_ = 0;
    endResetModel();
    emit pendingEditsChanged(false);
}
bool ResultTableModel::hasPendingEdits() const {
    return touchedCount_ != 0 || insertedCount_ != 0 || deletedCount_ != 0;
}

std::optional<ResultTableModel::CopyShape>
ResultTableModel::copyShape(const QItemSelection& selection, int scope) const {
    if (rows_.empty() || columns_.empty())
        return std::nullopt;
    CopyShape shape;
    shape.maxColumn = columnCount() - 1;
    if (scope == 2) {
        shape.rows.resize(rows_.size());
        std::iota(shape.rows.begin(), shape.rows.end(), 0);
        return shape;
    }
    // Clip each range once; overlapping ranges are deduplicated by the masks below.
    std::vector<QRect> ranges;
    const QRect page(0, 0, columnCount(), rowCount());
    for (const auto& range : selection) {
        if (!range.isValid() || range.model() != this)
            continue;
        const QRect clipped =
            QRect(QPoint(range.left(), range.top()), QPoint(range.right(), range.bottom()))
                .intersected(page);
        if (!clipped.isEmpty())
            ranges.push_back(clipped);
    }
    if (ranges.empty())
        return std::nullopt;
    if (scope == 1) {
        std::vector<bool> selected(rows_.size(), false);
        for (const auto& range : ranges)
            std::fill(selected.begin() + range.top(), selected.begin() + range.bottom() + 1, true);
        for (std::size_t row = 0; row < selected.size(); ++row)
            if (selected[row])
                shape.rows.push_back(static_cast<int>(row));
        return shape;
    }
    QRect bounds = ranges.front();
    for (const auto& range : ranges)
        bounds = bounds.united(range);
    shape.minColumn = bounds.left();
    shape.maxColumn = bounds.right();
    shape.rows.resize(static_cast<std::size_t>(bounds.height()));
    std::iota(shape.rows.begin(), shape.rows.end(), bounds.top());
    const auto width = static_cast<std::size_t>(bounds.width());
    shape.selected.assign(shape.rows.size() * width, false);
    for (const auto& range : ranges)
        for (int row = range.top(); row <= range.bottom(); ++row) {
            const auto offset = static_cast<std::size_t>(row - bounds.top()) * width;
            std::fill(shape.selected.begin() + offset + (range.left() - bounds.left()),
                      shape.selected.begin() + offset + (range.right() - bounds.left()) + 1, true);
        }
    return shape;
}

std::vector<std::pair<int, int>>
ResultTableModel::copyDeferredCells(const QItemSelection& selection, int scope) const {
    std::vector<std::pair<int, int>> deferred;
    const auto shape = copyShape(selection, scope);
    if (!shape)
        return deferred;
    for (std::size_t line = 0; line < shape->rows.size(); ++line)
        for (int column = shape->minColumn; column <= shape->maxColumn; ++column)
            if (shape->contains(line, column) &&
                std::holds_alternative<DeferredValue>(rows_[shape->rows[line]][column]))
                deferred.emplace_back(shape->rows[line], column);
    return deferred;
}

std::optional<ResultTableModel::CopySnapshot>
ResultTableModel::copySnapshot(const QItemSelection& selection, int scope,
                               const ResolvedCells& resolved) const {
    const auto shape = copyShape(selection, scope);
    if (!shape)
        return std::nullopt;
    CopySnapshot snapshot;
    snapshot.byteBudget = byteBudget_;
    for (const auto& [position, value] : resolved) {
        const auto [row, column] = position;
        std::optional<Cell> original;
        if (row >= 0 && row < rowCount() && column >= 0 && column < columnCount())
            original = rows_[row][column];
        snapshot.resolutions.push_back({std::move(original), value});
    }
    snapshot.rows.reserve(shape->rows.size());
    for (std::size_t line = 0; line < shape->rows.size(); ++line) {
        const int row = shape->rows[line];
        auto& output = snapshot.rows.emplace_back();
        output.reserve(static_cast<std::size_t>(shape->maxColumn - shape->minColumn + 1));
        for (int column = shape->minColumn; column <= shape->maxColumn; ++column) {
            if (!shape->contains(line, column)) {
                output.emplace_back(std::nullopt);
                continue;
            }
            const auto found = resolved.find({row, column});
            output.emplace_back(CopyCellSnapshot{
                rows_[row][column],
                found == resolved.end() ? std::nullopt : std::optional<Cell>(found->second),
                inserted_[row] && !touched_[row][column]});
        }
    }
    return snapshot;
}
} // namespace choscordb
