#include "design_system/tooltip/tooltip_style.h"

namespace choscordb::design {
QString tooltipStyleSheet() {
    return QStringLiteral(
        R"(QToolTip { background: @foreground; color: @background; border: 0; border-radius: 8px; padding: 6px 12px; font-size: 12px; }
)");
}

QString tooltipApplicationStyleSheet() {
    return QStringLiteral(R"(QToolTip { color: %17; background-color: %18; border: %2px solid %3; }
)");
}
} // namespace choscordb::design
