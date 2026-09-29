#include "bridge/cell_transport.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "models/result_table_model.h"
#include <algorithm>

namespace choscordb {
namespace {
rust::String transportString(const QString& value, bool& validUnicode) {
    validUnicode &= value.isValidUtf16();
    const auto bytes = value.toUtf8();
    return rust::String(bytes.constData(), static_cast<size_t>(bytes.size()));
}

QString text(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

ColumnDto transportColumn(const ResultColumn& value, bool& validUnicode) {
    ColumnDto result;
    result.name = transportString(value.name, validUnicode);
    result.database_type = transportString(value.databaseType, validUnicode);
    result.has_precision = value.precision.has_value();
    result.precision = value.precision.value_or(0);
    result.has_scale = value.scale.has_value();
    result.scale = value.scale.value_or(0);
    result.timezone = transportString(value.timezone, validUnicode);
    result.nullability = value.nullable ? (*value.nullable ? 1 : 0) : -1;
    return result;
}

CellDto transportCell(const Cell& value, bool& validUnicode) {
    return bridge_detail::cellDto(value, false, &validUnicode);
}

rust::Vec<ColumnDto> transportColumns(const std::vector<ResultColumn>& columns,
                                      bool& validUnicode) {
    rust::Vec<ColumnDto> result;
    for (const auto& column : columns)
        result.push_back(transportColumn(column, validUnicode));
    return result;
}

JsonViewRowDto transportRow(const ResultTableModel::Row& row, const std::vector<bool>& touched,
                            bool inserted, bool& validUnicode) {
    JsonViewRowDto result;
    result.inserted = inserted;
    for (const auto& cell : row)
        result.cells.push_back(transportCell(cell, validUnicode));
    for (bool touchedCell : touched)
        result.touched.push_back(touchedCell ? 1 : 0);
    return result;
}

rust::Vec<JsonViewRowDto> transportRows(const std::vector<ResultTableModel::Row>& rows,
                                        const std::vector<std::vector<bool>>& touched,
                                        const std::vector<bool>& inserted, bool& validUnicode) {
    rust::Vec<JsonViewRowDto> result;
    for (size_t row = 0; row < rows.size(); ++row)
        result.push_back(transportRow(rows[row], touched[row], inserted[row], validUnicode));
    return result;
}

QString unavailableDescription(const ResultTableModel::Row& row) {
    for (const auto& cell : row)
        if (const auto* value = std::get_if<UnavailableValue>(&cell))
            return QObject::tr("This row contains an unavailable %1 value: %2")
                .arg(value->databaseType, value->reason);
    return {};
}

QString unavailableDescription(const std::vector<ResultTableModel::Row>& rows) {
    for (const auto& row : rows) {
        const auto description = unavailableDescription(row);
        if (!description.isEmpty())
            return description;
    }
    return {};
}

QString resultError(const rust::String& code, std::size_t budget, bool cell,
                    const QString& unavailable) {
    const auto kind = text(code);
    const auto displayBytes =
        std::min<std::size_t>(budget / sizeof(QChar) * sizeof(QChar), 16 * 1024 * 1024);
    if (kind == "deferred")
        return QObject::tr("Load every deferred value before viewing this row as JSON.");
    if (kind == "invalid_resolution")
        return QObject::tr("A loaded row value is invalid.");
    if (kind == "incomplete_resolution")
        return QObject::tr("A loaded row value is invalid or incomplete.");
    if (kind == "non_finite")
        return QObject::tr("This row contains a non-finite number that JSON cannot represent.");
    if (kind == "display_limit")
        return QObject::tr("The JSON output exceeds %1 bytes of display storage (16 MiB maximum).")
            .arg(displayBytes);
    if (kind == "encoded_limit")
        return QObject::tr("The JSON output exceeds 16 MiB of encoded text.");
    if (kind == "invalid_json")
        return QObject::tr("This JSON/JSONB value is invalid or exceeds the 16 MiB JSON limit.");
    if (kind == "unavailable")
        return unavailable.isEmpty() ? QObject::tr("This row contains an unavailable value.")
                                     : unavailable;
    return cell ? QObject::tr("This cell contains invalid JSON view input.")
                : QObject::tr("This row contains invalid JSON view input.");
}

bool finish(JsonViewResultDto result, QString* json, QString* error, std::size_t budget,
            bool cell = false, const QString& unavailable = {}) {
    if (json)
        json->clear();
    if (error)
        error->clear();
    if (!result.error.empty()) {
        if (error)
            *error = resultError(result.error, budget, cell, unavailable);
        return false;
    }
    if (json)
        *json = text(result.json);
    return true;
}

ResultTableModel::RowJsonReadiness rowReadiness(const rust::String& value, QString* error,
                                                const QString& unavailable) {
    if (error)
        error->clear();
    const auto kind = text(value);
    if (kind == "ready")
        return ResultTableModel::RowJsonReadiness::Ready;
    if (kind == "needs_deferred")
        return ResultTableModel::RowJsonReadiness::NeedsDeferred;
    if (error)
        *error = unavailable.isEmpty() ? QObject::tr("This row contains invalid JSON view input.")
                                       : unavailable;
    return ResultTableModel::RowJsonReadiness::Invalid;
}
} // namespace

bool ResultTableModel::rowJson(int row, QString* json, QString* error,
                               const std::map<int, Cell>& resolved) const {
    return rowJsonImpl(row, json, error, resolved, false, nullptr);
}

ResultTableModel::RowJsonReadiness ResultTableModel::rowJsonReadiness(int row,
                                                                      QString* error) const {
    if (row < 0 || row >= rowCount()) {
        if (error)
            *error = tr("This result row is no longer available.");
        return RowJsonReadiness::Invalid;
    }
    bool validUnicode = true;
    auto columns = transportColumns(columns_, validUnicode);
    auto rowValue = transportRow(rows_[row], touched_[row], inserted_[row], validUnicode);
    return rowReadiness(json_row_readiness_policy(std::move(columns), std::move(rowValue),
                                                  byteBudget_, validUnicode),
                        error, unavailableDescription(rows_[row]));
}

bool ResultTableModel::rowJsonImpl(int row, QString* json, QString* error,
                                   const std::map<int, Cell>& resolved, bool allowDeferred,
                                   bool* unresolved) const {
    if (json)
        json->clear();
    if (error)
        error->clear();
    if (unresolved)
        *unresolved = false;
    if (!json || row < 0 || row >= rowCount()) {
        if (error)
            *error = tr("This result row is no longer available.");
        return false;
    }
    if (allowDeferred) {
        const auto state = rowJsonReadiness(row, error);
        if (unresolved)
            *unresolved = state == RowJsonReadiness::NeedsDeferred;
        return state != RowJsonReadiness::Invalid;
    }
    bool validUnicode = true;
    auto columns = transportColumns(columns_, validUnicode);
    auto rowValue = transportRow(rows_[row], touched_[row], inserted_[row], validUnicode);
    rust::Vec<JsonResolvedCellDto> values;
    for (const auto& [column, cell] : resolved) {
        if (column < 0) {
            if (error)
                *error = tr("A loaded row value is invalid.");
            return false;
        }
        JsonResolvedCellDto entry;
        entry.row = 0;
        entry.column = static_cast<uint32_t>(column);
        entry.value = transportCell(cell, validUnicode);
        values.push_back(std::move(entry));
    }
    return finish(render_json_row_policy(std::move(columns), std::move(rowValue), std::move(values),
                                         byteBudget_, validUnicode),
                  json, error, byteBudget_, false, unavailableDescription(rows_[row]));
}

ResultTableModel::CellJsonReadiness ResultTableModel::cellJsonReadiness(const QModelIndex& index,
                                                                        QString* error) const {
    if (error)
        error->clear();
    const auto value = cellValue(index);
    if (!value)
        return CellJsonReadiness::Unavailable;
    bool validUnicode = true;
    auto column = transportColumn(columns_[index.column()], validUnicode);
    auto cell = transportCell(*value, validUnicode);
    const auto result =
        text(json_cell_readiness_policy(std::move(column), std::move(cell), validUnicode));
    if (result == "ready")
        return CellJsonReadiness::Ready;
    if (result == "needs_deferred")
        return CellJsonReadiness::NeedsDeferred;
    if (result == "invalid") {
        if (error)
            *error = tr("This JSON/JSONB value is invalid or exceeds the 16 MiB JSON limit.");
        return CellJsonReadiness::Invalid;
    }
    return CellJsonReadiness::Unavailable;
}

bool ResultTableModel::cellJson(const QModelIndex& index, QString* json, QString* error,
                                const std::optional<Cell>& resolved) const {
    if (json)
        json->clear();
    if (error)
        error->clear();
    const auto original = cellValue(index);
    if (!json || !original) {
        if (error)
            *error = tr("This result cell is no longer available.");
        return false;
    }
    bool validUnicode = true;
    auto column = transportColumn(columns_[index.column()], validUnicode);
    auto cell = transportCell(*original, validUnicode);
    rust::Vec<CellDto> loaded;
    if (resolved)
        loaded.push_back(transportCell(*resolved, validUnicode));
    return finish(render_json_cell_policy(std::move(column), std::move(cell), std::move(loaded),
                                          byteBudget_, validUnicode),
                  json, error, byteBudget_, true);
}

bool ResultTableModel::pageJson(QString* json, QString* error,
                                const std::map<std::pair<int, int>, Cell>& resolved) const {
    if (json)
        json->clear();
    if (error)
        error->clear();
    if (!json || rowCount() == 0) {
        if (error)
            *error = tr("This result page is empty or no longer available.");
        return false;
    }
    bool validUnicode = true;
    auto columns = transportColumns(columns_, validUnicode);
    auto rows = transportRows(rows_, touched_, inserted_, validUnicode);
    rust::Vec<JsonResolvedCellDto> values;
    for (const auto& [position, cell] : resolved) {
        if (position.first < 0 || position.second < 0) {
            if (error)
                *error = tr("A loaded row value is invalid.");
            return false;
        }
        JsonResolvedCellDto entry;
        entry.row = static_cast<uint32_t>(position.first);
        entry.column = static_cast<uint32_t>(position.second);
        entry.value = transportCell(cell, validUnicode);
        values.push_back(std::move(entry));
    }
    return finish(render_json_page_policy(std::move(columns), std::move(rows), std::move(values),
                                          byteBudget_, validUnicode),
                  json, error, byteBudget_, false, unavailableDescription(rows_));
}

ResultTableModel::RowJsonReadiness ResultTableModel::pageJsonReadiness(QString* error) const {
    if (rowCount() == 0) {
        if (error)
            *error = tr("This result page is empty or no longer available.");
        return RowJsonReadiness::Invalid;
    }
    bool validUnicode = true;
    auto columns = transportColumns(columns_, validUnicode);
    auto rows = transportRows(rows_, touched_, inserted_, validUnicode);
    return rowReadiness(
        json_page_readiness_policy(std::move(columns), std::move(rows), byteBudget_, validUnicode),
        error, unavailableDescription(rows_));
}

std::optional<ResultTableModel::JsonViewSnapshot>
ResultTableModel::jsonViewSnapshot(JsonViewScope scope, int row, int column,
                                   const ResolvedCells& resolved) const {
    if (rows_.empty() || (scope != JsonViewScope::Page && (row < 0 || row >= rowCount())) ||
        (scope == JsonViewScope::Cell && (column < 0 || column >= columnCount())))
        return std::nullopt;
    JsonViewSnapshot snapshot;
    snapshot.scope = scope;
    snapshot.columns = columns_;
    snapshot.byteBudget = byteBudget_;
    snapshot.column = column;
    if (scope == JsonViewScope::Page) {
        snapshot.rows = rows_;
        snapshot.touched = touched_;
        snapshot.inserted = inserted_;
        snapshot.resolved = resolved;
    } else {
        snapshot.rows.push_back(rows_[row]);
        snapshot.touched.push_back(touched_[row]);
        snapshot.inserted.push_back(inserted_[row]);
        for (const auto& [position, value] : resolved)
            if (position.first == row &&
                (scope != JsonViewScope::Cell || position.second == column))
                snapshot.resolved.emplace(std::pair{0, position.second}, value);
    }
    return snapshot;
}

ResultTableModel::JsonViewEvaluation ResultTableModel::evaluateJsonView(JsonViewSnapshot snapshot) {
    JsonViewEvaluation evaluation;
    if (snapshot.rows.empty() || snapshot.touched.size() != snapshot.rows.size() ||
        snapshot.inserted.size() != snapshot.rows.size()) {
        evaluation.error = QObject::tr("This result is no longer available.");
        return evaluation;
    }
    bool validUnicode = true;
    auto columns = transportColumns(snapshot.columns, validUnicode);
    auto rows = transportRows(snapshot.rows, snapshot.touched, snapshot.inserted, validUnicode);
    rust::Vec<JsonResolvedCellDto> resolved;
    for (const auto& [position, value] : snapshot.resolved) {
        if (position.first < 0 || position.second < 0) {
            evaluation.error = QObject::tr("A loaded row value is invalid.");
            return evaluation;
        }
        JsonResolvedCellDto entry;
        entry.row = static_cast<uint32_t>(position.first);
        entry.column = static_cast<uint32_t>(position.second);
        entry.value = transportCell(value, validUnicode);
        resolved.push_back(std::move(entry));
    }
    JsonViewResultDto result;
    rust::String readiness;
    const auto budget = snapshot.byteBudget;
    switch (snapshot.scope) {
    case JsonViewScope::Cell: {
        const auto column = static_cast<size_t>(snapshot.column);
        if (column >= snapshot.columns.size() || snapshot.rows.empty() ||
            column >= snapshot.rows[0].size()) {
            evaluation.state = JsonViewState::Unavailable;
            return evaluation;
        }
        auto original = transportCell(snapshot.rows[0][column], validUnicode);
        rust::Vec<CellDto> loaded;
        if (const auto found = snapshot.resolved.find({0, snapshot.column});
            found != snapshot.resolved.end())
            loaded.push_back(transportCell(found->second, validUnicode));
        auto readinessColumn = transportColumn(snapshot.columns[column], validUnicode);
        auto readinessCell = transportCell(snapshot.rows[0][column], validUnicode);
        auto renderColumn = transportColumn(snapshot.columns[column], validUnicode);
        readiness = json_cell_readiness_policy(std::move(readinessColumn), std::move(readinessCell),
                                               validUnicode);
        result = render_json_cell_policy(std::move(renderColumn), std::move(original),
                                         std::move(loaded), budget, validUnicode);
        break;
    }
    case JsonViewScope::Row: {
        auto readinessColumns = transportColumns(snapshot.columns, validUnicode);
        auto readinessRow =
            transportRow(snapshot.rows[0], snapshot.touched[0], snapshot.inserted[0], validUnicode);
        auto renderRow =
            transportRow(snapshot.rows[0], snapshot.touched[0], snapshot.inserted[0], validUnicode);
        readiness = json_row_readiness_policy(std::move(readinessColumns), std::move(readinessRow),
                                              budget, validUnicode);
        result = render_json_row_policy(std::move(columns), std::move(renderRow),
                                        std::move(resolved), budget, validUnicode);
        break;
    }
    case JsonViewScope::Page: {
        auto readinessColumns = transportColumns(snapshot.columns, validUnicode);
        auto readinessRows =
            transportRows(snapshot.rows, snapshot.touched, snapshot.inserted, validUnicode);
        readiness = json_page_readiness_policy(std::move(readinessColumns),
                                               std::move(readinessRows), budget, validUnicode);
        result = render_json_page_policy(std::move(columns), std::move(rows), std::move(resolved),
                                         budget, validUnicode);
        break;
    }
    }
    if (result.error.empty()) {
        evaluation.state = JsonViewState::Ready;
        evaluation.json = text(result.json);
        return evaluation;
    }
    const auto status = text(readiness);
    if (status == "unavailable")
        evaluation.state = JsonViewState::Unavailable;
    else if (status == "needs_deferred" && text(result.error) == "deferred")
        evaluation.state = JsonViewState::NeedsDeferred;
    else
        evaluation.state = JsonViewState::Invalid;
    evaluation.error = resultError(result.error, budget, snapshot.scope == JsonViewScope::Cell,
                                   unavailableDescription(snapshot.rows));
    return evaluation;
}
} // namespace choscordb
