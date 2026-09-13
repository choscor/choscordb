#pragma once
#include <QColor>
#include <QIcon>
#include <QList>
namespace choscordb::design {
enum class Icon {
    AppMark,
    Run,
    Cancel,
    Add,
    Refresh,
    Close,
    ChevronDown,
    ChevronRight,
    ChevronLeft,
    Search,
    Database,
    Check,
    Warning,
    Error,
    Loader,
    Copy,
    Export
};
struct IconDefinition final {
    Icon role;
    QString name;
    QString source;
};
[[nodiscard]] QList<IconDefinition> iconCatalog();
[[nodiscard]] QString iconResourcePath(Icon icon);
[[nodiscard]] bool iconResourceDecodes(Icon icon);
[[nodiscard]] QIcon themedIcon(Icon icon, const QColor& color, int size);
} // namespace choscordb::design
