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
    Prohibit,
    Add,
    Refresh,
    Close,
    CaretDown,
    CaretRight,
    CaretLeft,
    CaretUpDown,
    Search,
    Database,
    PostgreSQL,
    SQLite,
    MySQL,
    MariaDB,
    Check,
    CheckCircle,
    Settings,
    Warning,
    Error,
    Loader,
    Copy,
    Export,
    Code,
    Table,
    GridFour,
    Folder,
    File,
    Key,
    Link,
    Eye,
    EyeSlash,
    Trash,
    ArrowUp,
    ArrowDown
};
struct IconDefinition final {
    Icon role;
    QString name;
    QString source;
};
[[nodiscard]] QList<IconDefinition> iconCatalog();
[[nodiscard]] QString iconResourcePath(Icon icon);
[[nodiscard]] QIcon themedIcon(Icon icon, const QColor& color, int size);
[[nodiscard]] Icon driverIcon(const QString& driver);
}
