#include "bridge/engine_adapter_p.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace choscordb::engine_adapter_detail {
ObjectGraph parseObjectGraph(const rust::String& payload, bool* ok) {
    ObjectGraph graph;
    const QByteArray bytes(payload.data(), static_cast<qsizetype>(payload.size()));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    *ok = error.error == QJsonParseError::NoError && document.isObject();
    if (!*ok)
        return graph;
    const auto root = document.object();
    const auto availability = root.value(QStringLiteral("availability")).toString();
    graph.availability =
        availability == QStringLiteral("Unsupported")   ? MetadataAvailability::Unsupported
        : availability == QStringLiteral("Unavailable") ? MetadataAvailability::Unavailable
                                                        : MetadataAvailability::Available;
    graph.reason = root.value(QStringLiteral("reason")).toString();
    for (const auto& warning : root.value(QStringLiteral("warnings")).toArray())
        graph.warnings.append(warning.toString());
    for (const auto& value : root.value(QStringLiteral("tables")).toArray()) {
        const auto tableJson = value.toObject();
        ObjectGraphTable table;
        table.id = tableJson.value(QStringLiteral("id")).toString();
        table.qualifiedName = tableJson.value(QStringLiteral("qualified_name")).toString();
        for (const auto& item : tableJson.value(QStringLiteral("columns")).toArray()) {
            const auto column = item.toObject();
            table.columns.append({column.value(QStringLiteral("name")).toString(),
                                  column.value(QStringLiteral("database_type")).toString(),
                                  column.value(QStringLiteral("primary_key")).toBool(),
                                  column.value(QStringLiteral("foreign_key")).toBool()});
        }
        graph.tables.append(table);
    }
    for (const auto& value : root.value(QStringLiteral("edges")).toArray()) {
        const auto edgeJson = value.toObject();
        ObjectGraphEdge edge;
        edge.id = edgeJson.value(QStringLiteral("id")).toString();
        edge.sourceId = edgeJson.value(QStringLiteral("source_id")).toString();
        edge.targetId = edgeJson.value(QStringLiteral("target_id")).toString();
        for (const auto& column : edgeJson.value(QStringLiteral("source_columns")).toArray())
            edge.sourceColumns.append(column.toString());
        for (const auto& column : edgeJson.value(QStringLiteral("target_columns")).toArray())
            edge.targetColumns.append(column.toString());
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
    if (d_->graphs.size() >= 64) {
        emit objectGraphFailed(connection, object, requestToken,
                               tr("Too many pending ER diagram requests"));
        return;
    }
    const auto token = d_->nextInspectionToken++;
    d_->graphs.insert(token, {connection, requestToken, object});
    const auto bytes = object.toUtf8();
    auto reply = object_graph_request(*d_->engine, connection,
                                      engine_adapter_detail::utf8View(bytes), token);
    if (!reply.accepted) {
        d_->graphs.remove(token);
        emit objectGraphFailed(connection, object, requestToken,
                               engine_adapter_detail::string(reply.error));
    }
}
} // namespace choscordb
