#include "bridge/result_column_adapter.h"

#include "choscordb-bridge/src/lib.rs.h"

namespace choscordb {
namespace {
QString text(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
} // namespace

ResultColumn resultColumn(const ColumnDto& column) {
    std::optional<bool> nullable;
    if (column.nullability == 0 || column.nullability == 1)
        nullable = column.nullability == 1;
    return {text(column.name),
            text(column.database_type),
            column.has_precision ? std::optional<quint32>(column.precision) : std::nullopt,
            column.has_scale ? std::optional<qint32>(column.scale) : std::nullopt,
            text(column.timezone),
            nullable};
}
} // namespace choscordb
