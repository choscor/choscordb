#include "bridge/cell_transport.h"
#include "bridge/rust_text.h"
#include "models/result_table_model.h"
#include <algorithm>

namespace choscordb {
namespace {
rust::String transportString(const QString& value, bool& validUnicode) {
    validUnicode &= value.isValidUtf16();
    const auto bytes = value.toUtf8();
    return rust::String(bytes.constData(), static_cast<size_t>(bytes.size()));
}

using bridge_detail::fromRust;

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
    const auto kind = fromRust(code);
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

} // namespace

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
        evaluation.json = fromRust(result.json);
        return evaluation;
    }
    const auto status = fromRust(readiness);
    if (status == "unavailable")
        evaluation.state = JsonViewState::Unavailable;
    else if (status == "needs_deferred" && fromRust(result.error) == "deferred")
        evaluation.state = JsonViewState::NeedsDeferred;
    else
        evaluation.state = JsonViewState::Invalid;
    evaluation.error = resultError(result.error, budget, snapshot.scope == JsonViewScope::Cell,
                                   unavailableDescription(snapshot.rows));
    return evaluation;
}
} // namespace choscordb
