#pragma once

#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"

#include <QCoreApplication>
#include <QString>

namespace choscordb {
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

inline QString objectKindTitle(const QString& kind) {
    if (kind == QLatin1String("index"))
        return QCoreApplication::translate("ObjectKind", "Index");
    if (kind == QLatin1String("sequence"))
        return QCoreApplication::translate("ObjectKind", "Sequence");
    return QCoreApplication::translate("ObjectKind", "Function");
}

inline QString keyKindTitle(const QString& kind) {
    if (kind == QLatin1String("primarykey"))
        return QCoreApplication::translate("ObjectKind", "Primary key");
    if (kind == QLatin1String("foreignkey"))
        return QCoreApplication::translate("ObjectKind", "Foreign key");
    if (kind == QLatin1String("uniquekey"))
        return QCoreApplication::translate("ObjectKind", "Unique key");
    return kind;
}

inline QString relationKindPhrase(const QString& kind, const QString& relationSubtype) {
    if (relationSubtype == QLatin1String("materialized_view"))
        return QCoreApplication::translate("ObjectKind", "materialized view");
    if (relationSubtype == QLatin1String("foreign_table"))
        return QCoreApplication::translate("ObjectKind", "foreign table");
    return kind;
}

inline int objectIconSize() {
    return design::dimension(design::Dimension::IconSmall);
}
}
