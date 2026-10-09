#include "bridge/cell_transport.h"
#include "bridge/engine_adapter_p.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <QHash>
#include <QMutex>
#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace choscordb {
using engine_adapter_detail::fromRust;
using engine_adapter_detail::toRust;
using engine_adapter_detail::utf8View;
namespace {
GridEditRequestDto gridRequestDto(const GridEditRequest& request, bool eligibilityOnly) {
    GridEditRequestDto dto;
    dto.driver = toRust(request.driver);
    dto.qualified_name = toRust(request.qualifiedName);
    dto.parameter_style = toRust(request.parameterStyle);
    dto.reason = toRust(request.reason);
    dto.object_read_only = request.objectReadOnly;
    for (const auto& column : request.columns) {
        GridEditColumnDto entry;
        entry.name = toRust(column.name);
        entry.result_name = toRust(column.resultName);
        entry.database_type = toRust(column.databaseType);
        entry.key = column.key;
        entry.generated = column.generated;
        entry.enum_source_column = toRust(column.enumSourceColumn);
        for (const auto& choice : column.enumChoices)
            entry.enum_choices.push_back(toRust(choice));
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
    const auto kind = fromRust(value.kind);
    if (kind == "null")
        return std::monostate{};
    if (kind == "boolean")
        return value.boolean;
    if (kind == "integer")
        return value.integer;
    if (kind == "real")
        return value.real;
    if (kind == "decimal")
        return DecimalValue{fromRust(value.text)};
    if (kind == "binary") {
        QByteArray bytes;
        bytes.reserve(static_cast<qsizetype>(value.bytes.size()));
        for (auto byte : value.bytes)
            bytes.append(static_cast<char>(byte));
        return bytes;
    }
    return fromRust(value.text);
}
} // namespace
bool EngineAdapter::applyEditBatch(quint64 connection,
                                   const std::vector<ReviewedEditStatement>& statements,
                                   quint64 token) {
    rust::Vec<EditStatementDto> batch;
    for (const auto& statement : statements) {
        EditStatementDto dto;
        dto.sql = toRust(statement.sql);
        dto.has_expected_rows = statement.expectedRows.has_value();
        dto.expected_rows = statement.expectedRows.value_or(0);
        if (statement.params.size() != statement.paramKinds.size()) {
            emit commandFailed(tr("The reviewed edit values changed before application."));
            return false;
        }
        for (size_t i = 0; i < statement.params.size(); ++i) {
            auto cell = bridge_detail::cellDto(statement.params[i]);
            // Conversion guard: the planned kind below replaces the cell's own kind, so
            // a partial value must never reach that relabeling.
            if (cell.kind == "invalid_unicode" || cell.kind == "deferred" ||
                cell.kind == "deferred_fallback" || cell.kind == "fallback_text" ||
                cell.kind == "unavailable") {
                emit commandFailed(tr("Deferred values cannot be bound to grid edits."));
                return false;
            }
            cell.kind = toRust(statement.paramKinds[i]);
            dto.params.push_back(std::move(cell));
        }
        batch.push_back(std::move(dto));
    }
    auto reply = apply_edit_batch(*d_->engine, connection, std::move(batch), token);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
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
    result.reason = fromRust(source.reason);
    return result;
}
GridEditPlan EngineAdapter::planGridEdits(const GridEditRequest& request) {
    auto source = plan_grid_edits_policy(gridRequestDto(request, false));
    GridEditPlan result;
    result.error = fromRust(source.error);
    result.review = fromRust(source.review);
    for (const auto& planned : source.statements) {
        ReviewedEditStatement statement;
        statement.sql = fromRust(planned.statement.sql);
        if (planned.statement.has_expected_rows)
            statement.expectedRows = planned.statement.expected_rows;
        for (const auto& param : planned.statement.params) {
            statement.params.push_back(gridCell(param));
            statement.paramKinds.push_back(fromRust(param.kind));
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
        result.append({static_cast<CellFilterOperator>(item.operation), fromRust(item.label),
                       item.enabled, fromRust(item.reason)});
    return result;
}
CellFilterComposition EngineAdapter::composeQuickFilter(const QStringList& columns,
                                                        const QString& draft, const QString& column,
                                                        const Cell& value,
                                                        CellFilterOperator operation) {
    rust::Vec<rust::String> names;
    for (const auto& name : columns)
        names.push_back(toRust(name));
    const auto draftBytes = draft.toUtf8();
    const auto columnBytes = column.toUtf8();
    auto dto = bridge_detail::cellDto(value);
    if (!column.isValidUtf16() || !draft.isValidUtf16())
        dto.kind = "invalid_unicode";
    const auto result =
        quick_filter_compose_policy(std::move(names), utf8View(draftBytes), utf8View(columnBytes),
                                    std::move(dto), static_cast<QuickFilterOperator>(operation));
    return {fromRust(result.expression), fromRust(result.validation_error), fromRust(result.error)};
}
bool EngineAdapter::foreignKeyValueFilterable(const Cell& value) {
    auto dto = bridge_detail::cellDto(value, true);
    if (const auto* decimal = std::get_if<DecimalValue>(&value))
        dto.text = toRust(decimal->text);
    return foreign_key_value_filterable_policy(std::move(dto));
}
std::optional<QString> EngineAdapter::foreignKeyPredicate(const QString& targetColumn,
                                                          const Cell& value) {
    const auto bytes = targetColumn.toUtf8();
    const auto result =
        foreign_key_predicate_policy(utf8View(bytes), bridge_detail::cellDto(value, false));
    return result.valid ? std::optional<QString>{fromRust(result.expression)} : std::nullopt;
}
const GridCellPolicy& EngineAdapter::gridCellPolicy(GridCell kind) {
    static const auto policies = [] {
        std::array<GridCellPolicy, 5> result{};
        for (const auto& dto : grid_cell_kind_policies()) {
            const auto index = static_cast<std::size_t>(dto.kind);
            if (index < result.size())
                result[index] = {dto.inline_editable, dto.blocks_row_delete,
                                 dto.blocks_row_duplicate, dto.duplicate_requires_load};
        }
        return result;
    }();
    return policies[static_cast<std::size_t>(kind)];
}
QString EngineAdapter::gridRowInsertError(qsizetype rows) {
    return fromRust(grid_row_insert_error_policy(
        static_cast<quint32>(std::clamp<qsizetype>(rows, 0, std::numeric_limits<quint32>::max()))));
}
std::optional<Cell> EngineAdapter::parseGridEditValue(const QString& databaseType,
                                                      const QString& text, QString* error) {
    const auto typeBytes = databaseType.toUtf8();
    const auto textBytes = text.toUtf8();
    const auto parsed = parse_grid_edit_value_policy(utf8View(typeBytes), utf8View(textBytes));
    if (error)
        *error = fromRust(parsed.error);
    return parsed.valid ? std::optional<Cell>{gridCell(parsed.cell)} : std::nullopt;
}
ObjectKindTraits EngineAdapter::objectKindTraits(const QString& kind) {
    static QMutex mutex;
    static QHash<QString, ObjectKindTraits> cache;
    const QMutexLocker lock(&mutex);
    if (const auto found = cache.constFind(kind); found != cache.cend())
        return *found;
    const auto bytes = kind.toUtf8();
    const auto dto = object_kind_traits_policy(utf8View(bytes));
    ObjectKindTraits traits{dto.opens_object_tab,
                            dto.ddl,
                            dto.relation,
                            dto.container,
                            dto.pinnable,
                            dto.navigation_anchor,
                            dto.search_descends,
                            dto.completion_candidate,
                            dto.has_detail_pane ? int(dto.detail_pane) : -1,
                            dto.connection,
                            dto.column,
                            dto.diagram,
                            dto.has_initial_pane ? int(dto.initial_pane) : -1,
                            dto.repeats_across_parents};
    cache.insert(kind, traits);
    return traits;
}
DriverWorkflow EngineAdapter::driverWorkflow(const QString& driver) {
    const auto bytes = driver.toUtf8();
    const auto dto = driver_workflow_policy(utf8View(bytes));
    return {dto.inspect_after_result, dto.sql_mode_before_execution};
}
const TransactionGuard& EngineAdapter::transactionGuard(bool transactionActive) {
    static const auto guard = [](bool active) {
        const auto dto = transaction_guard_policy(active);
        return TransactionGuard{fromRust(dto.apply_edits), fromRust(dto.enable_auto_commit)};
    };
    static const TransactionGuard idle = guard(false), open = guard(true);
    return transactionActive ? open : idle;
}
const NavigatorSearchBudget& EngineAdapter::navigatorSearchBudget() {
    static const NavigatorSearchBudget budget = [] {
        const auto dto = navigator_search_budget();
        return NavigatorSearchBudget{int(dto.quick_visits), int(dto.quick_requests),
                                     int(dto.quick_results), int(dto.filter_visits),
                                     int(dto.filter_requests)};
    }();
    return budget;
}
bool EngineAdapter::sidebarChildVisible(const QString& parentKind, const QString& kind) {
    static QMutex mutex;
    static QHash<std::pair<QString, QString>, bool> cache;
    const QMutexLocker lock(&mutex);
    const auto key = std::pair{parentKind, kind};
    if (const auto found = cache.constFind(key); found != cache.cend())
        return *found;
    const auto parentBytes = parentKind.toUtf8();
    const auto kindBytes = kind.toUtf8();
    const bool visible = sidebar_child_visible_policy(utf8View(parentBytes), utf8View(kindBytes));
    cache.insert(key, visible);
    return visible;
}
QString EngineAdapter::objectActionUnavailableReason(bool rename, const QString& driver,
                                                     const QString& kind, const QString& subtype) {
    const auto driverBytes = driver.toUtf8();
    const auto kindBytes = kind.toUtf8();
    const auto subtypeBytes = subtype.toUtf8();
    return fromRust(object_action_unavailable_reason(rename, utf8View(driverBytes),
                                                     utf8View(kindBytes), utf8View(subtypeBytes)));
}
bool EngineAdapter::navigatorObjectVisible(const QString& driver, bool showSystemSchemas,
                                           const QString& qualifiedName) {
    const auto driverBytes = driver.toUtf8();
    const auto nameBytes = qualifiedName.toUtf8();
    return navigator_object_visible_policy(utf8View(driverBytes), showSystemSchemas,
                                           utf8View(nameBytes));
}
bool EngineAdapter::systemSchemaNode(const QString& kind, const QString& name) {
    const auto kindBytes = kind.toUtf8(), nameBytes = name.toUtf8();
    return system_schema_node_policy(utf8View(kindBytes), utf8View(nameBytes));
}
bool EngineAdapter::systemSchemasHidden(const QString& driver, bool showSystemSchemas) {
    const auto bytes = driver.toUtf8();
    return system_schemas_hidden_policy(utf8View(bytes), showSystemSchemas);
}
QString EngineAdapter::sqlExportTableError(const QStringList& table) {
    rust::Vec<rust::String> parts;
    parts.reserve(static_cast<size_t>(table.size()));
    for (const auto& part : table)
        parts.push_back(toRust(part));
    return fromRust(sql_export_table_error(std::move(parts)));
}
bool EngineAdapter::appearanceThemeValid(const QString& theme) {
    const auto bytes = theme.toUtf8();
    return appearance_theme_valid(utf8View(bytes));
}
} // namespace choscordb
