#include "bridge/engine_adapter_p.h"

namespace choscordb {
using engine_adapter_detail::fromRust;
using engine_adapter_detail::fromRustStrings;
using engine_adapter_detail::toRust;
using engine_adapter_detail::toRustStrings;
namespace {
QList<CodeLabel> codeLabels(const rust::Vec<CodeLabelDto>& values) {
    QList<CodeLabel> result;
    result.reserve(static_cast<qsizetype>(values.size()));
    for (const auto& value : values)
        result.push_back({fromRust(value.code), fromRust(value.label)});
    return result;
}
TableExportRequestDto requestDto(const TableExportRequest& request) {
    TableExportRequestDto dto;
    dto.driver = toRust(request.driver);
    for (const auto& object : request.objects) {
        TableExportObjectDto entry;
        entry.object_id = toRust(object.objectId);
        entry.qualified_name = toRust(object.qualifiedName);
        entry.kind = toRust(object.kind);
        entry.relation_subtype = toRust(object.relationSubtype);
        dto.objects.push_back(std::move(entry));
    }
    dto.format = toRust(request.format);
    dto.delimiter = toRust(request.delimiter);
    dto.quote = toRust(request.quote);
    dto.line_break = toRust(request.lineBreak);
    dto.null_text = toRust(request.nullText);
    dto.header = request.header;
    dto.structure = request.structure;
    dto.drop = request.drop;
    dto.data = request.data;
    dto.folder = request.folder;
    dto.path = toRust(request.path);
    dto.pattern = toRust(request.pattern);
    return dto;
}
TableImportRequestDto importDto(const TableImportRequest& request) {
    TableImportRequestDto dto;
    dto.driver = toRust(request.driver);
    dto.path = toRust(request.path);
    dto.format = toRust(request.format);
    dto.delimiter = toRust(request.delimiter);
    dto.quote = toRust(request.quote);
    dto.header = request.header;
    dto.null_text = toRust(request.nullText);
    dto.encoding = toRust(request.encoding);
    dto.existing = request.existing;
    dto.object_id = toRust(request.objectId);
    dto.parent_kind = toRust(request.parentKind);
    dto.parent_qualified = toRust(request.parentQualified);
    dto.table_name = toRust(request.tableName);
    for (const auto& entry : request.mapping) {
        TableImportMappingDto mapping;
        mapping.target = toRust(entry.target);
        mapping.source = entry.source;
        dto.mapping.push_back(std::move(mapping));
    }
    dto.conflict = toRust(request.conflict);
    return dto;
}
QList<TableExportFormatChoice> formatChoices(const rust::Vec<TableExportFormatDto>& values) {
    QList<TableExportFormatChoice> result;
    for (const auto& format : values)
        result.push_back({fromRust(format.code), fromRust(format.label), format.csv_options,
                          format.sql_options});
    return result;
}
}
QList<TableExportFormatChoice> EngineAdapter::tableExportFormats() {
    return formatChoices(table_export_formats());
}
QList<CodeLabel> EngineAdapter::tableExportLineBreaks() {
    return codeLabels(table_export_line_breaks());
}
TableExportRequest EngineAdapter::tableExportDefaults() {
    const auto dto = table_export_defaults();
    TableExportRequest request;
    request.format = fromRust(dto.format);
    request.delimiter = fromRust(dto.delimiter);
    request.quote = fromRust(dto.quote);
    request.lineBreak = fromRust(dto.line_break);
    request.nullText = fromRust(dto.null_text);
    request.header = dto.header;
    request.structure = dto.structure;
    request.drop = dto.drop;
    request.data = dto.data;
    request.folder = dto.folder;
    request.pattern = fromRust(dto.pattern);
    return request;
}
QString EngineAdapter::tableExportError(const TableExportRequest& request) {
    return fromRust(table_export_error(requestDto(request)));
}
std::optional<quint64>
EngineAdapter::exportTables(quint64 connection, const TableExportRequest& request, QString* error) {
    const auto reply = export_tables(*d_->engine, connection, requestDto(request));
    if (!reply.accepted) {
        if (error)
            *error = fromRust(reply.error);
        return std::nullopt;
    }
    return reply.id;
}
QString EngineAdapter::tableImportUnavailableReason(const QString& kind, const QString& subtype) {
    return fromRust(table_import_unavailable_reason(toRust(kind), toRust(subtype)));
}
QList<TableExportFormatChoice> EngineAdapter::tableImportFormats() {
    return formatChoices(table_import_formats());
}
QList<CodeLabel> EngineAdapter::tableImportEncodings() {
    return codeLabels(table_import_encodings());
}
QList<CodeLabel> EngineAdapter::tableImportConflicts(const QString& driver) {
    return codeLabels(table_import_conflicts(toRust(driver)));
}
TableImportRequest EngineAdapter::tableImportDefaults() {
    const auto dto = table_import_defaults();
    TableImportRequest request;
    request.format = fromRust(dto.format);
    request.delimiter = fromRust(dto.delimiter);
    request.quote = fromRust(dto.quote);
    request.header = dto.header;
    request.nullText = fromRust(dto.null_text);
    request.encoding = fromRust(dto.encoding);
    request.conflict = fromRust(dto.conflict);
    return request;
}
QList<int> EngineAdapter::tableImportDefaultMapping(const QStringList& tableColumns,
                                                    const QStringList& fileColumns) {
    QList<int> result;
    for (const auto source :
         table_import_default_mapping(toRustStrings(tableColumns), toRustStrings(fileColumns)))
        result.push_back(source);
    return result;
}
QString EngineAdapter::tableImportError(const TableImportRequest& request) {
    return fromRust(table_import_error(importDto(request)));
}
TableImportPreview EngineAdapter::tableImportPreview(const TableImportRequest& request) {
    const auto dto = table_import_preview(importDto(request));
    TableImportPreview preview;
    preview.valid = dto.valid;
    preview.error = fromRust(dto.error);
    preview.columns = fromRustStrings(dto.columns);
    preview.rowCount = static_cast<int>(dto.row_count);
    preview.cells = fromRustStrings(dto.cells);
    preview.nulls.reserve(static_cast<qsizetype>(dto.nulls.size()));
    for (const auto null : dto.nulls)
        preview.nulls.push_back(null);
    return preview;
}
TableImportReview EngineAdapter::tableImportReview(const TableImportRequest& request,
                                                   const QString& qualifiedName,
                                                   const QStringList& tableColumns,
                                                   const QStringList& keyColumns,
                                                   const QStringList& fileColumns) {
    const auto dto =
        table_import_review(importDto(request), toRust(qualifiedName), toRustStrings(tableColumns),
                            toRustStrings(keyColumns), toRustStrings(fileColumns));
    return {dto.valid, fromRust(dto.error), fromRust(dto.sql)};
}
std::optional<quint64>
EngineAdapter::importTable(quint64 connection, const TableImportRequest& request, QString* error) {
    const auto reply = import_table(*d_->engine, connection, importDto(request));
    if (!reply.accepted) {
        if (error)
            *error = fromRust(reply.error);
        return std::nullopt;
    }
    return reply.id;
}
}
