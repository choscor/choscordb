#pragma once
#include <QColor>
#include <QIcon>
#include <QList>
namespace choscordb::design {
enum class Icon {
    AppMark,
    Run,
    Cancel,
    Square,
    Add,
    Refresh,
    Close,
    ChevronDown,
    ChevronRight,
    ChevronLeft,
    Search,
    Database,
    PostgreSQL,
    SQLite,
    MySQL,
    Check,
    Warning,
    Error,
    Loader,
    Copy,
    Export,
    Code,
    Table,
    Folder,
    File,
    Key,
    Eye,
    EyeOff
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
