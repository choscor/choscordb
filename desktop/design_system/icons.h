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
    Commit,
    Rollback,
    Settings,
    Warning,
    Error,
    Loader,
    Copy,
    Export,
    Code,
    Table,
    Grid2x2,
    Folder,
    File,
    Key,
    Link,
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
[[nodiscard]] QIcon themedIcon(Icon icon, const QColor& color, int size);
// The logo for a connection driver id ("sqlite", "postgres", "mysql"); Database otherwise.
[[nodiscard]] Icon driverIcon(const QString& driver);
} // namespace choscordb::design
