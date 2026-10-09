#include "bridge/engine_adapter_p.h"

namespace choscordb::engine_adapter_detail {
ObjectGraph objectGraph(const ObjectGraphDto& dto) {
    ObjectGraph graph;
    graph.availability =
        dto.availability == GraphAvailabilityDto::Unsupported   ? MetadataAvailability::Unsupported
        : dto.availability == GraphAvailabilityDto::Unavailable ? MetadataAvailability::Unavailable
                                                                : MetadataAvailability::Available;
    graph.reason = fromRust(dto.reason);
    for (const auto& warning : dto.warnings)
        graph.warnings.append(fromRust(warning));
    for (const auto& source : dto.tables) {
        ObjectGraphTable table;
        table.id = fromRust(source.id);
        table.qualifiedName = fromRust(source.qualified_name);
        for (const auto& column : source.columns)
            table.columns.append({fromRust(column.name), fromRust(column.database_type),
                                  column.primary_key, column.foreign_key});
        graph.tables.append(table);
    }
    for (const auto& source : dto.edges) {
        ObjectGraphEdge edge;
        edge.id = fromRust(source.id);
        edge.sourceId = fromRust(source.source_id);
        edge.targetId = fromRust(source.target_id);
        for (const auto& column : source.source_columns)
            edge.sourceColumns.append(fromRust(column));
        for (const auto& column : source.target_columns)
            edge.targetColumns.append(fromRust(column));
        graph.edges.append(edge);
    }
    return graph;
}
} // namespace choscordb::engine_adapter_detail

namespace choscordb {
void EngineAdapter::loadObjectGraph(quint64 connection, const QString& object,
                                    quint64 requestToken) {
    for (auto it = d_->graphs.begin(); it != d_->graphs.end();) {
        if (it->connection == connection && it->object == object)
            it = d_->graphs.erase(it);
        else
            ++it;
    }
    const auto token = d_->nextInspectionToken++;
    d_->graphs.insert(token, {connection, requestToken, object});
    const auto bytes = object.toUtf8();
    auto reply = object_graph_request(*d_->engine, connection,
                                      engine_adapter_detail::utf8View(bytes), token);
    if (!reply.accepted) {
        d_->graphs.remove(token);
        emit objectGraphFailed(connection, object, requestToken,
                               engine_adapter_detail::fromRust(reply.error));
    }
}
} // namespace choscordb
