#pragma once

#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"

#include <QString>

namespace choscordb {
// One mapping from a database object kind to its icon, shared by the navigator,
// workspace tabs, recovery and quick search. Connections choose a driver icon
// at their call site because the kind alone does not identify the driver.
inline design::Icon objectKindIcon(const QString& kind) {
    if (kind == QLatin1String("schema") || kind == QLatin1String("database"))
        return design::Icon::Folder;
    if (kind == QLatin1String("view"))
        return design::Icon::Eye;
    if (kind == QLatin1String("table"))
        return design::Icon::Grid2x2;
    if (kind == QLatin1String("index") || kind.contains(QLatin1String("key")))
        return design::Icon::Key;
    return design::Icon::File;
}

// Navigator rows, document tabs and quick-search rows show object icons at one size.
inline int objectIconSize() {
    return design::dimension(design::Dimension::IconSmall);
}
} // namespace choscordb
