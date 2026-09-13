#include "design_system/toolbar/toolbar_style.h"

namespace choscordb::design {
QString toolbarStyleSheet() {
    return QStringLiteral(R"(QToolBar { spacing: 4px; border: 0; background: @background; }
QToolBar::separator { background: @border; width: 1px; margin: 4px; }
)");
}

QString toolbarApplicationStyleSheet() {
    return QStringLiteral(
        R"(QToolBar { min-height: %14px; spacing: %15px; border: 0; border-bottom: %2px solid %3; }
)");
}
} // namespace choscordb::design
