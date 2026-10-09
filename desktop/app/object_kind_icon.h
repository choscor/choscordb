#pragma once

#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"

#include <QCoreApplication>
#include <QString>

namespace choscordb {
// Object-kind presentation only: icons, row styles and translated labels. Behavior
// that depends on a kind uses Rust's EngineAdapter::objectKindTraits instead.

// One mapping from a database object kind to its icon, shared by the navigator,
// workspace tabs, recovery and quick search. Connections choose a driver icon
// at their call site because the kind alone does not identify the driver. Saved
// SQL rows use the "folder" and "file" display kinds.
inline design::Icon objectKindIcon(const QString& kind) {
    if (kind == QLatin1String("schema") || kind == QLatin1String("database") ||
        kind == QLatin1String("folder"))
        return design::Icon::Folder;
    if (kind == QLatin1String("view"))
        return design::Icon::Eye;
    if (kind == QLatin1String("table"))
        return design::Icon::Grid2x2;
    if (kind == QLatin1String("index") || kind.contains(QLatin1String("key")))
        return design::Icon::Key;
    return design::Icon::File;
}

// How a navigator row draws its kind: columns use the column-row layout, groups
// show no icon, and connections take their driver's icon.
enum class NavigatorRowStyle { Object, Column, Group, Connection };
inline NavigatorRowStyle navigatorRowStyle(const QString& kind) {
    if (kind == QLatin1String("column"))
        return NavigatorRowStyle::Column;
    if (kind == QLatin1String("group"))
        return NavigatorRowStyle::Group;
    if (kind == QLatin1String("connection"))
        return NavigatorRowStyle::Connection;
    return NavigatorRowStyle::Object;
}

// The title shown for an object tab whose kind has no column layout.
inline QString objectKindTitle(const QString& kind) {
    if (kind == QLatin1String("index"))
        return QCoreApplication::translate("ObjectKind", "Index");
    if (kind == QLatin1String("sequence"))
        return QCoreApplication::translate("ObjectKind", "Sequence");
    return QCoreApplication::translate("ObjectKind", "Function");
}

// The label for a key row in an object tab's Keys pane.
inline QString keyKindTitle(const QString& kind) {
    if (kind == QLatin1String("primarykey"))
        return QCoreApplication::translate("ObjectKind", "Primary key");
    if (kind == QLatin1String("foreignkey"))
        return QCoreApplication::translate("ObjectKind", "Foreign key");
    if (kind == QLatin1String("uniquekey"))
        return QCoreApplication::translate("ObjectKind", "Unique key");
    return kind;
}

// The phrase naming a relation in action prompts, such as "materialized view".
inline QString relationKindPhrase(const QString& kind, const QString& relationSubtype) {
    if (relationSubtype == QLatin1String("materialized_view"))
        return QCoreApplication::translate("ObjectKind", "materialized view");
    if (relationSubtype == QLatin1String("foreign_table"))
        return QCoreApplication::translate("ObjectKind", "foreign table");
    return kind;
}

// Navigator rows, document tabs and quick-search rows show object icons at one size.
inline int objectIconSize() {
    return design::dimension(design::Dimension::IconSmall);
}
} // namespace choscordb
