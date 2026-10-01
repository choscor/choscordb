#include "bridge/cell_transport.h"
#include "bridge/engine_adapter_p.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <utility>

namespace choscordb {
using engine_adapter_detail::rustString;
using engine_adapter_detail::string;
using engine_adapter_detail::utf8View;
namespace {
GridEditRequestDto gridRequestDto(const GridEditRequest& request, bool eligibilityOnly) {
    GridEditRequestDto dto;
    dto.driver = rustString(request.driver);
    dto.qualified_name = rustString(request.qualifiedName);
    dto.parameter_style = rustString(request.parameterStyle);
    dto.reason = rustString(request.reason);
    dto.object_read_only = request.objectReadOnly;
    for (const auto& column : request.columns) {
        GridEditColumnDto entry;
        entry.name = rustString(column.name);
        entry.result_name = rustString(column.resultName);
        entry.database_type = rustString(column.databaseType);
        entry.key = column.key;
        entry.generated = column.generated;
        entry.enum_source_column = rustString(column.enumSourceColumn);
        for (const auto& choice : column.enumChoices)
            entry.enum_choices.push_back(rustString(choice));
        dto.columns.push_back(std::move(entry));
    }
    for (const auto& row : request.rows) {
        GridEditRowDto entry;
        for (const auto& cell : row.current)
            entry.current.push_back(bridge_detail::cellDto(cell, eligibilityOnly));
        for (const auto& cell : row.original)
            entry.original.push_back(bridge_detail::cellDto(cell, eligibilityOnly));
        for (bool touched : row.touched)
            entry.touched.push_back(touched ? 1 : 0);
        entry.inserted = row.inserted;
        entry.deleted = row.deleted;
        dto.rows.push_back(std::move(entry));
    }
    return dto;
}
Cell gridCell(const CellDto& value) {
    const auto kind = string(value.kind);
    if (kind == "null")
        return std::monostate{};
    if (kind == "boolean")
        return value.boolean;
    if (kind == "integer")
        return value.integer;
    if (kind == "real")
        return value.real;
    if (kind == "decimal")
        return DecimalValue{string(value.text)};
    if (kind == "binary") {
        QByteArray bytes;
        bytes.reserve(static_cast<qsizetype>(value.bytes.size()));
        for (auto byte : value.bytes)
            bytes.append(static_cast<char>(byte));
        return bytes;
    }
    return string(value.text);
}
} // namespace
bool EngineAdapter::applyEditBatch(quint64 connection,
                                   const std::vector<ReviewedEditStatement>& statements,
                                   quint64 token) {
    rust::Vec<EditStatementDto> batch;
    for (const auto& statement : statements) {
        EditStatementDto dto;
        dto.sql = rustString(statement.sql);
        dto.has_expected_rows = statement.expectedRows.has_value();
        dto.expected_rows = statement.expectedRows.value_or(0);
        if (statement.params.size() != statement.paramKinds.size()) {
            emit commandFailed(tr("The reviewed edit values changed before application."));
            return false;
        }
        for (size_t i = 0; i < statement.params.size(); ++i) {
            auto cell = bridge_detail::cellDto(statement.params[i]);
            if (cell.kind == "invalid_unicode" || cell.kind == "deferred" ||
                cell.kind == "deferred_fallback" || cell.kind == "fallback_text" ||
                cell.kind == "unavailable") {
                emit commandFailed(tr("Deferred values cannot be bound to grid edits."));
                return false;
            }
            cell.kind = rustString(statement.paramKinds[i]);
            dto.params.push_back(std::move(cell));
        }
        batch.push_back(std::move(dto));
    }
    auto reply = apply_edit_batch(*d_->engine, connection, std::move(batch), token);
    if (!reply.accepted)
        emit commandFailed(string(reply.error));
    return reply.accepted;
}
GridEditEligibility EngineAdapter::gridEditability(const GridEditRequest& request) {
    const auto source = grid_editability_policy(gridRequestDto(request, true));
    GridEditEligibility result;
    for (auto value : source.editable)
        result.editable.push_back(value != 0);
    for (auto value : source.insert_editable)
        result.insertEditable.push_back(value != 0);
    for (auto value : source.key_columns)
        result.keyColumns.push_back(value != 0);
    result.canInsert = source.can_insert;
    result.canDelete = source.can_delete;
    result.reason = string(source.reason);
    return result;
}
GridEditPlan EngineAdapter::planGridEdits(const GridEditRequest& request) {
    auto source = plan_grid_edits_policy(gridRequestDto(request, false));
    GridEditPlan result;
    result.error = string(source.error);
    for (const auto& planned : source.statements) {
        ReviewedEditStatement statement;
        statement.sql = string(planned.statement.sql);
        if (planned.statement.has_expected_rows)
            statement.expectedRows = planned.statement.expected_rows;
        for (const auto& param : planned.statement.params) {
            statement.params.push_back(gridCell(param));
            statement.paramKinds.push_back(string(param.kind));
        }
        result.statements.push_back(std::move(statement));
    }
    return result;
}
QList<CellFilterOption> EngineAdapter::quickFilterOptions(const QString& column,
                                                          const Cell& value) {
    const auto bytes = column.toUtf8();
    QList<CellFilterOption> result;
    auto dto = bridge_detail::cellDto(value);
    if (!column.isValidUtf16())
        dto.kind = "invalid_unicode";
    for (const auto& item : quick_filter_options_policy(utf8View(bytes), std::move(dto)))
        result.append({static_cast<CellFilterOperator>(item.operation), string(item.label),
                       item.enabled, string(item.reason)});
    return result;
}
CellFilterComposition EngineAdapter::composeQuickFilter(const QStringList& columns,
                                                        const QString& draft, const QString& column,
                                                        const Cell& value,
                                                        CellFilterOperator operation) {
    rust::Vec<rust::String> names;
    for (const auto& name : columns)
        names.push_back(rustString(name));
    const auto draftBytes = draft.toUtf8();
    const auto columnBytes = column.toUtf8();
    auto dto = bridge_detail::cellDto(value);
    if (!column.isValidUtf16() || !draft.isValidUtf16())
        dto.kind = "invalid_unicode";
    const auto result =
        quick_filter_compose_policy(std::move(names), utf8View(draftBytes), utf8View(columnBytes),
                                    std::move(dto), static_cast<QuickFilterOperator>(operation));
    return {string(result.expression), string(result.validation_error), string(result.error)};
}
bool EngineAdapter::foreignKeyValueFilterable(const Cell& value) {
    auto dto = bridge_detail::cellDto(value, true);
    if (const auto* decimal = std::get_if<DecimalValue>(&value))
        dto.text = rustString(decimal->text);
    return foreign_key_value_filterable_policy(std::move(dto));
}
std::optional<QString> EngineAdapter::foreignKeyPredicate(const QString& targetColumn,
                                                          const Cell& value) {
    const auto bytes = targetColumn.toUtf8();
    const auto result =
        foreign_key_predicate_policy(utf8View(bytes), bridge_detail::cellDto(value, false));
    return result.valid ? std::optional<QString>{string(result.expression)} : std::nullopt;
}
std::optional<Cell> EngineAdapter::parseGridEditValue(const QString& databaseType,
                                                      const QString& text, QString* error) {
    const auto typeBytes = databaseType.toUtf8();
    const auto textBytes = text.toUtf8();
    const auto parsed = parse_grid_edit_value_policy(utf8View(typeBytes), utf8View(textBytes));
    if (error)
        *error = string(parsed.error);
    return parsed.valid ? std::optional<Cell>{gridCell(parsed.cell)} : std::nullopt;
}
bool EngineAdapter::navigatorObjectVisible(const QString& driver, bool showSystemSchemas,
                                           const QString& qualifiedName) {
    const auto driverBytes = driver.toUtf8();
    const auto nameBytes = qualifiedName.toUtf8();
    return navigator_object_visible_policy(utf8View(driverBytes), showSystemSchemas,
                                           utf8View(nameBytes));
}
bool EngineAdapter::postgresSystemSchema(const QString& schema) {
    const auto bytes = schema.toUtf8();
    return postgres_system_schema_policy(utf8View(bytes));
}
bool EngineAdapter::appearanceThemeValid(const QString& theme) {
    const auto bytes = theme.toUtf8();
    return appearance_theme_valid(utf8View(bytes));
}
} // namespace choscordb
