#include "bridge/result_column_adapter.h"

#include "bridge/rust_text.h"

namespace choscordb {
using bridge_detail::fromRust;

ResultColumn resultColumn(const ColumnDto& column) {
    std::optional<bool> nullable;
    if (column.nullability == 0 || column.nullability == 1)
        nullable = column.nullability == 1;
    return {fromRust(column.name),
            fromRust(column.database_type),
            column.has_precision ? std::optional<quint32>(column.precision) : std::nullopt,
            column.has_scale ? std::optional<qint32>(column.scale) : std::nullopt,
            fromRust(column.timezone),
            nullable};
}
} // namespace choscordb
