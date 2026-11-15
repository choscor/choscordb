#include "bridge/engine_adapter_p.h"

namespace choscordb {
using engine_adapter_detail::fromRust;
using engine_adapter_detail::toRust;
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
}
QList<TableExportFormatChoice> EngineAdapter::tableExportFormats() {
    QList<TableExportFormatChoice> result;
    for (const auto& format : table_export_formats())
        result.push_back({fromRust(format.code), fromRust(format.label), format.csv_options,
                          format.sql_options});
    return result;
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
}
