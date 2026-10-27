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
GridCell gridCellKind(const Cell& cell) {
    if (std::holds_alternative<QByteArray>(cell))
        return GridCell::Binary;
    if (std::holds_alternative<DeferredValue>(cell))
        return GridCell::Deferred;
    if (std::holds_alternative<FallbackText>(cell))
        return GridCell::FallbackText;
    if (std::holds_alternative<UnavailableValue>(cell))
        return GridCell::Unavailable;
    return GridCell::Value;
}
QString valueText(const Cell& cell) {
    return std::visit(
        [](const auto& value) -> QString {
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
                return value.fallback
                           ? ResultTableModel::tr("[text fallback · %1 · %2 bytes · open to load]")
                                 .arg(value.type)
                                 .arg(value.bytes)
                           : QString("[%1 · %2 bytes · open to load]")
                                 .arg(value.type)
                                 .arg(value.bytes);
            else if constexpr (std::is_same_v<T, FallbackText>) {
                constexpr qsizetype PreviewChars = 128;
                const auto preview = value.text.left(PreviewChars);
                return ResultTableModel::tr("[text fallback · %1] %2%3")
                    .arg(value.databaseType, preview,
                         value.text.size() > PreviewChars ? QStringLiteral("…") : QString{});
            } else if constexpr (std::is_same_v<T, UnavailableValue>)
                return ResultTableModel::tr("[unavailable · %1]").arg(value.databaseType);
            else
                return QString{};
        },
        cell);
}
// Long or multi-line text is shown as a bounded single-line preview.
QString displayText(const Cell& cell) {
    const auto* value = std::get_if<QString>(&cell);
    if (!value)
        return valueText(cell);
    const auto isBreak = [](QChar character) {
        return character == QLatin1Char('\n') || character == QLatin1Char('\r') ||
               character == QChar::LineSeparator || character == QChar::ParagraphSeparator;
    };
    const auto limit = std::min(value->size(), ResultTableModel::DisplayPreviewChars);
    const auto begin = value->cbegin();
    if (value->size() <= ResultTableModel::DisplayPreviewChars &&
        std::none_of(begin, begin + limit, isBreak))
        return *value;
    QString preview = value->left(limit);
    for (auto& character : preview)
        if (isBreak(character))
            character = QLatin1Char(' ');
    if (value->size() > ResultTableModel::DisplayPreviewChars)
        preview += QChar(0x2026);
    return preview;
}
// The text that `display` shares with `cell` is already charged with the cell.
bool sharesText(const QString& display, const Cell& cell) {
    if (const auto* text = std::get_if<QString>(&cell))
        return display.isSharedWith(*text);
    if (const auto* decimal = std::get_if<DecimalValue>(&cell))
        return display.isSharedWith(decimal->text);
    return false;
}
QString headerType(const ResultColumn& column) {
    QString type = column.databaseType;
    if (column.precision && !type.contains('(')) {
        type += '(' + QString::number(*column.precision);
        if (column.scale)
            type += ',' + QString::number(*column.scale);
        type += ')';
    }
    return type;
}
// The bits of a staged insert row; its cells and their edits are charged separately.
std::size_t insertedRowBytes(std::size_t columns) {
    return columns * sizeof(Cell) + sizeof(ResultTableModel::Row) + 2;
}
} // namespace
ResultTableModel::ResultTableModel(QObject* parent, std::size_t byteBudget)
    : QAbstractTableModel(parent), byteBudget_(byteBudget) {
    for (std::size_t kind = 0; kind < GridCellKinds; ++kind)
        cellPolicies_[kind] = EngineAdapter::gridCellPolicy(GridCell(kind));
}
const GridCellPolicy& ResultTableModel::cellPolicy(const Cell& cell) const {
    return cellPolicies_[std::size_t(gridCellKind(cell))];
}
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

const ResultTableModel::CellEdit* ResultTableModel::edit(std::size_t row,
                                                         std::size_t column) const {
    if (edits_.empty())
        return nullptr;
    const auto found = edits_.find(cellKey(row, column));
    return found == edits_.end() ? nullptr : &found->second;
}
bool ResultTableModel::touched(std::size_t row, std::size_t column) const {
    return edit(row, column) != nullptr;
}
std::vector<bool> ResultTableModel::touchedRow(std::size_t row) const {
    std::vector<bool> result(columns_.size(), false);
    if (!edits_.empty())
        for (std::size_t column = 0; column < result.size(); ++column)
            result[column] = touched(row, column);
    return result;
}
std::size_t ResultTableModel::editBytes(int row, int column) const {
    const auto* value = edit(row, column);
    return value ? value->bytes : 0;
}
bool ResultTableModel::cellFilterable(int row, int column) const {
    if (const auto* value = edit(row, column))
        return value->filterable;
    // Staged inserts follow the page rows, and their unstaged cells are omitted.
    return static_cast<std::size_t>(row) < originalRowCount_ &&
           filterable_[static_cast<std::size_t>(row) * columns_.size() +
                       static_cast<std::size_t>(column)];
}
bool ResultTableModel::linkable(int row, int column) const {
    return row >= 0 && column >= 0 && row < rowCount() && column < columnCount() &&
           linkColumns_[column] && cellFilterable(row, column);
}

void ResultTableModel::resetPendingCounts() {
    editedOriginals_.clear();
    edits_.clear();
    originalRowCount_ = rows_.size();
    insertedCount_ = deletedCount_ = 0;
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
bool ResultTableModel::validIndex(const QModelIndex& index) const {
    return index.isValid() && index.model() == this && index.row() >= 0 && index.column() >= 0 &&
           index.row() < rowCount() && index.column() < columnCount();
}
QVariant ResultTableModel::data(const QModelIndex& index, int role) const {
    return validIndex(index) ? cellData(index.row(), index.column(), role) : QVariant();
}
void ResultTableModel::multiData(const QModelIndex& index, QModelRoleDataSpan roleDataSpan) const {
    const bool valid = validIndex(index);
    for (auto& roleData : roleDataSpan)
        roleData.setData(valid ? cellData(index.row(), index.column(), roleData.role())
                               : QVariant());
}
QVariant ResultTableModel::cellData(int row, int column, int role) const {
    const auto& cell = rows_[row][column];
    const bool staged = touched(row, column);
    // The delegate maps staged-change state to theme surfaces.
    if (role == design::CellChangeRole)
        return deleted_[row]    ? int(design::CellChange::Deleted)
               : inserted_[row] ? int(design::CellChange::Inserted)
               : staged         ? int(design::CellChange::Changed)
                                : int(design::CellChange::None);
    if (role == design::ForeignKeyLinkRole)
        return linkable(row, column);
    if (role == Qt::ToolTipRole && deleted_[row])
        return tr("Pending deletion");
    if (role == Qt::ToolTipRole && inserted_[row])
        return staged ? tr("Pending insert value") : tr("Omitted; database default applies");
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
        return staged && std::holds_alternative<std::monostate>(cell);
    if (role == design::CellNullRole)
        return std::holds_alternative<std::monostate>(cell);
    if (role == design::ChoiceLabelsRole && cellEditable(row, column) &&
        column < static_cast<int>(cellMetadata_.size())) {
        const auto& metadata = cellMetadata_[column];
        if (metadata.boolean)
            return QStringList{QStringLiteral("true"), QStringLiteral("false")};
        return metadata.enumChoices;
    }
    if (role == design::ChoiceNullableRole && column < static_cast<int>(cellMetadata_.size()))
        return cellMetadata_[column].nullable == true;
    if (role == design::ForeignKeyLinkLabelRole) {
        if (!linkable(row, column))
            return {};
        const auto& linked = cellMetadata_[column];
        return tr("Open referenced row in %1 (%2)")
            .arg(linked.targetQualifiedName, linked.targetColumn);
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    if (inserted_[row] && !staged)
        return QString{};
    if (role == Qt::EditRole)
        return valueText(cell);
    if (const auto* value = edit(row, column))
        return value->display;
    return display_[static_cast<std::size_t>(row) * columns_.size() +
                    static_cast<std::size_t>(column)];
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
    if (role == HeaderTypeRole)
        return headerTypes_[section];
    if (role == Qt::DisplayRole) {
        const auto& type = headerTypes_[section];
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
ResultTableModel::PreparedPage ResultTableModel::preparePage(std::vector<ResultColumn> columns,
                                                             std::vector<Row> rows,
                                                             std::vector<bool> filterable,
                                                             quint64 firstRow) {
    PreparedPage page{std::move(columns), std::move(rows), {}, std::move(filterable), {},
                      firstRow,           std::nullopt};
    const auto cells = page.rows.size() * page.columns.size();
    // Rust bounds every page to its page-size limit.
    if (page.columns.size() > std::numeric_limits<int>::max() ||
        firstRow > std::numeric_limits<quint64>::max() - page.rows.size() ||
        std::any_of(page.rows.begin(), page.rows.end(),
                    [&](const auto& row) { return row.size() != page.columns.size(); }) ||
        (!page.filterable.empty() && page.filterable.size() != cells))
        return page;
    page.filterable.resize(cells, false);
    page.display.reserve(cells);
    for (const auto& row : page.rows)
        for (const auto& cell : row)
            page.display.push_back(displayText(cell));
    page.headerTypes.reserve(page.columns.size());
    for (const auto& column : page.columns)
        page.headerTypes.push_back(headerType(column));

    AllocationCounter count{std::numeric_limits<std::size_t>::max()};
    count.add(page.columns.capacity(), sizeof(ResultColumn));
    count.add(page.rows.capacity(), sizeof(Row));
    for (const auto& column : page.columns) {
        count.string(column.name);
        count.string(column.databaseType);
        count.string(column.timezone);
    }
    for (const auto& row : page.rows) {
        count.add(row.capacity(), sizeof(Cell));
        for (const auto& cell : row)
            std::visit(
                [&count](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, QString>)
                        count.string(value);
                    else if constexpr (std::is_same_v<T, DecimalValue>)
                        count.string(value.text);
                    else if constexpr (std::is_same_v<T, QByteArray>)
                        count.binary(value);
                    else if constexpr (std::is_same_v<T, DeferredValue>)
                        count.string(value.type);
                    else if constexpr (std::is_same_v<T, FallbackText>) {
                        count.string(value.text);
                        count.string(value.databaseType);
                    } else if constexpr (std::is_same_v<T, UnavailableValue>) {
                        count.string(value.databaseType);
                        count.string(value.reason);
                    }
                },
                cell);
    }
    count.add(page.display.capacity(), sizeof(QString));
    for (std::size_t cell = 0; cell < cells; ++cell)
        if (!sharesText(page.display[cell],
                        page.rows[cell / page.columns.size()][cell % page.columns.size()]))
            count.string(page.display[cell]);
    count.add(page.headerTypes.capacity(), sizeof(QString));
    for (const auto& type : page.headerTypes)
        count.string(type);
    // The row and column flags, and the originals that an edit keeps beside the visible page.
    count.add((cells + 7) / 8);
    count.add(page.rows.size() * 2 + page.columns.size() * 5);
    count.add(page.rows.size(), sizeof(Row));
    for (const auto& row : page.rows)
        count.add(row.size(), sizeof(Cell));
    page.bytes = count.bytes;
    return page;
}
bool ResultTableModel::setPage(PreparedPage page, std::vector<ResultCellMetadata> metadata) {
    if (!page.bytes || *page.bytes > byteBudget_ ||
        (!metadata.empty() && metadata.size() != page.columns.size()))
        return false;
    beginResetModel();
    // Destroy the previous page before adopting the validated transfer.
    std::vector<ResultColumn>().swap(columns_);
    std::vector<ResultCellMetadata>().swap(cellMetadata_);
    std::vector<Row>().swap(rows_);
    std::vector<QString>().swap(display_);
    columns_ = std::move(page.columns);
    rows_ = std::move(page.rows);
    display_ = std::move(page.display);
    filterable_ = std::move(page.filterable);
    headerTypes_ = std::move(page.headerTypes);
    resetPendingCounts();
    linkColumns_.assign(columns_.size(), false);
    inserted_.assign(rows_.size(), false);
    deleted_.assign(rows_.size(), false);
    editable_.assign(columns_.size(), false);
    insertEditable_.assign(columns_.size(), false);
    keyColumns_.assign(columns_.size(), false);
    canInsert_ = canDelete_ = false;
    firstRow_ = page.firstRow;
    residentBytes_ = *page.bytes;
    stagedBytes_ = 0;
    if (!metadata.empty())
        applyCellMetadata(std::move(metadata));
    endResetModel();
    emit pendingEditsChanged(false);
    return true;
}
bool ResultTableModel::setPage(std::vector<ResultColumn> columns, std::vector<Row> rows,
                               quint64 firstRow, std::vector<ResultCellMetadata> metadata) {
    return setPage(preparePage(std::move(columns), std::move(rows), {}, firstRow),
                   std::move(metadata));
}
bool ResultTableModel::setCellMetadata(std::vector<ResultCellMetadata> metadata) {
    if (metadata.size() != columns_.size())
        return false;
    if (metadata == cellMetadata_)
        return true;
    applyCellMetadata(std::move(metadata));
    if (rowCount() && columnCount())
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                         {design::ChoiceLabelsRole, design::ChoiceNullableRole,
                          design::ForeignKeyLinkLabelRole, design::ForeignKeyLinkRole});
    return true;
}
void ResultTableModel::applyCellMetadata(std::vector<ResultCellMetadata> metadata) {
    // Rust bounds catalog labels before they cross the bridge.
    cellMetadata_ = std::move(metadata);
    linkColumns_.assign(columns_.size(), false);
    for (std::size_t column = 0; column < cellMetadata_.size(); ++column) {
        const auto& item = cellMetadata_[column];
        linkColumns_[column] = !item.sourceColumn.isEmpty() && !item.sourceObject.isEmpty() &&
                               !item.targetObject.isEmpty() &&
                               !item.targetQualifiedName.isEmpty() && !item.targetColumn.isEmpty();
    }
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
    if (insertEditable.size() != columns_.size())
        insertEditable.assign(columns_.size(), canInsert);
    if (editable == editable_ && insertEditable == insertEditable_ && canInsert == canInsert_ &&
        canDelete == canDelete_)
        return;
    editable_ = std::move(editable);
    insertEditable_ = std::move(insertEditable);
    canInsert_ = canInsert;
    canDelete_ = canDelete;
    if (!rows_.empty() && !columns_.empty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}
bool ResultTableModel::cellEditable(int row, int column) const {
    if (row < 0 || column < 0 || row >= rowCount() || column >= columnCount() || deleted_[row] ||
        !(inserted_[row] ? insertEditable_[column] : editable_[column]))
        return false;
    return cellPolicy(rows_[row][column]).inlineEditable;
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
std::size_t ResultTableModel::editCharge(const Cell& value, const QString& display) {
    // A hash node holds the key, the edit and about two pointers of bookkeeping.
    AllocationCounter charge{std::numeric_limits<std::size_t>::max()};
    charge.add(sizeof(std::pair<const quint64, CellEdit>) + 2 * sizeof(void*));
    if (!sharesText(display, value))
        charge.string(display);
    return charge.bytes;
}
void ResultTableModel::stageValue(int row, int column, Cell value, QString display,
                                  std::size_t bytes) {
    preserveOriginal(row);
    const bool filterable = EngineAdapter::foreignKeyValueFilterable(value);
    rows_[row][column] = std::move(value);
    auto& entry = edits_[cellKey(row, column)];
    stagedBytes_ = stagedBytes_ - entry.bytes + bytes;
    entry = {bytes, std::move(display), filterable};
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
    const auto parsedBytes =
        std::size_t(text.size()) * sizeof(QChar) + sizeof(Cell) + QtHeaderBytes;
    const auto available =
        byteBudget_ - residentBytes_ - (stagedBytes_ - editBytes(index.row(), index.column()));
    if (parsedBytes > available)
        return false;
    auto converted = EngineAdapter::parseGridEditValue(columns_[index.column()].databaseType, text);
    if (!converted)
        return false;
    auto display = displayText(*converted);
    const auto bytes = parsedBytes + editCharge(*converted, display);
    if (bytes > available)
        return false;
    stageValue(index.row(), index.column(), std::move(*converted), std::move(display), bytes);
    emit dataChanged(index, index);
    emit pendingEditsChanged(hasPendingEdits());
    return true;
}
ResultTableModel::CellEditSnapshot ResultTableModel::cellEditSnapshot(const QModelIndex& index,
                                                                      const QString& text) const {
    if (index.model() != this || !(flags(index) & Qt::ItemIsEditable))
        return {};
    return {columns_[index.column()].databaseType, text,
            byteBudget_ - residentBytes_ - (stagedBytes_ - editBytes(index.row(), index.column())),
            true};
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
    AllocationCounter charge{byteBudget_ - residentBytes_ -
                             (stagedBytes_ - editBytes(index.row(), index.column()))};
    auto display = displayText(*evaluation.value);
    const bool fits = charge.add(sizeof(Cell)) &&
                      charge.add(editCharge(*evaluation.value, display)) &&
                      std::visit(
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
    stageValue(index.row(), index.column(), std::move(*evaluation.value), std::move(display),
               evaluation.bytes);
    emit dataChanged(index, index);
    emit pendingEditsChanged(hasPendingEdits());
    return evaluation;
}
bool ResultTableModel::stageNull(int row, int column) {
    const Cell null;
    auto display = displayText(null);
    const auto bytes = editCharge(null, display);
    if (bytes > byteBudget_ - residentBytes_ - (stagedBytes_ - editBytes(row, column)))
        return false;
    stageValue(row, column, null, std::move(display), bytes);
    return true;
}
bool ResultTableModel::setNull(const QModelIndex& index) {
    if (!(flags(index) & Qt::ItemIsEditable) || !stageNull(index.row(), index.column()))
        return false;
    emit dataChanged(index, index);
    emit pendingEditsChanged(hasPendingEdits());
    return true;
}
bool ResultTableModel::setNull(const QItemSelection& selection) {
    // Stage all of the selection or none of it.
    const Cell null;
    const auto charge = editCharge(null, displayText(null));
    std::size_t needed = 0, released = 0;
    for (const auto& range : selection) {
        if (!range.isValid() || range.model() != this)
            continue;
        const int bottom = std::min(rowCount() - 1, range.bottom());
        const int right = std::min(columnCount() - 1, range.right());
        for (int column = std::max(0, range.left()); column <= right; ++column)
            for (int row = std::max(0, range.top()); row <= bottom; ++row)
                if (cellEditable(row, column)) {
                    needed += charge;
                    released += editBytes(row, column);
                }
    }
    if (needed > byteBudget_ - residentBytes_ - stagedBytes_ + released)
        return false;
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
                if (!cellEditable(row, column) || !stageNull(row, column))
                    continue;
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
    const auto bytes = insertedRowBytes(columns_.size());
    if (!canInsert_ || !EngineAdapter::gridRowInsertError(qsizetype(rows_.size())).isEmpty() ||
        bytes > byteBudget_ - residentBytes_ - stagedBytes_)
        return false;
    beginInsertRows({}, rowCount(), rowCount());
    rows_.emplace_back(columns_.size());
    inserted_.push_back(true);
    deleted_.push_back(false);
    ++insertedCount_;
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
    if (const auto full = EngineAdapter::gridRowInsertError(qsizetype(rows_.size()));
        !full.isEmpty())
        return fail(full);
    if (std::any_of(rows_[row].begin(), rows_[row].end(),
                    [this](const Cell& value) { return cellPolicy(value).blocksRowDuplicate; }))
        return fail(tr("Rows with fallback or unavailable values cannot be duplicated."));
    Row duplicate(columns_.size());
    std::vector<std::pair<std::size_t, CellEdit>> copied;
    auto bytes = insertedRowBytes(columns_.size());
    for (size_t column = 0; column < columns_.size(); ++column) {
        if (!insertEditable_[column] || !copyable[column])
            continue;
        if (cellPolicy(rows_[row][column]).duplicateRequiresLoad)
            return fail(tr("Load a complete value before duplicating this row."));
        duplicate[column] = rows_[row][column];
        auto display = displayText(duplicate[column]);
        const auto charge = editCharge(duplicate[column], display);
        bytes += charge;
        copied.emplace_back(column, CellEdit{charge, std::move(display),
                                             cellFilterable(row, static_cast<int>(column))});
    }
    if (bytes > byteBudget_ - residentBytes_ - stagedBytes_)
        return fail(tr("Duplicating this row would exceed the result grid memory limit."));
    beginInsertRows({}, rowCount(), rowCount());
    for (auto& [column, edit] : copied)
        edits_.emplace(cellKey(rows_.size(), column), std::move(edit));
    rows_.push_back(std::move(duplicate));
    inserted_.push_back(true);
    deleted_.push_back(false);
    ++insertedCount_;
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
            beginRemoveRows({}, row, row);
            eraseInsertedRow(row);
            endRemoveRows();
        } else if (canDelete_ &&
                   (!deleted ||
                    std::none_of(rows_[row].begin(), rows_[row].end(), [this](const Cell& cell) {
                        return cellPolicy(cell).blocksRowDelete;
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
void ResultTableModel::eraseInsertedRow(int row) {
    // Staged inserts are the last rows, so only their edits move down.
    stagedBytes_ -= insertedRowBytes(columns_.size());
    for (std::size_t column = 0; column < columns_.size(); ++column)
        if (const auto found = edits_.find(cellKey(row, column)); found != edits_.end()) {
            stagedBytes_ -= found->second.bytes;
            edits_.erase(found);
        }
    for (auto next = static_cast<std::size_t>(row) + 1; next < rows_.size(); ++next)
        for (std::size_t column = 0; column < columns_.size(); ++column)
            if (auto moved = edits_.extract(cellKey(next, column))) {
                moved.key() = cellKey(next - 1, column);
                edits_.insert(std::move(moved));
            }
    --insertedCount_;
    rows_.erase(rows_.begin() + row);
    inserted_.erase(inserted_.begin() + row);
    deleted_.erase(deleted_.begin() + row);
}
void ResultTableModel::discardEdits() {
    beginResetModel();
    rows_.resize(originalRowCount_);
    for (auto& [row, original] : editedOriginals_)
        rows_[row] = std::move(original);
    resetPendingCounts();
    inserted_.assign(rows_.size(), false);
    deleted_.assign(rows_.size(), false);
    stagedBytes_ = 0;
    endResetModel();
    emit pendingEditsChanged(false);
}
bool ResultTableModel::hasPendingEdits() const {
    return !edits_.empty() || insertedCount_ != 0 || deletedCount_ != 0;
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
                inserted_[row] && !touched(row, column)});
        }
    }
    return snapshot;
}
} // namespace choscordb
