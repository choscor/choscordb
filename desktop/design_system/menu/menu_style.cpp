#include "design_system/menu/menu_style.h"

namespace choscordb::design {
QString menuStyleSheet() {
    return QStringLiteral(
        R"(QMenu { min-width: 243px; margin: @shadowMargin; background: @popover; color: @popoverText; border: 1px solid @border; border-radius: @popoverRadius; padding: 5px; }
QMenu::item { min-width: 209px; min-height: 17px; padding: 6px 24px 6px 10px; border-radius: 4px; font-size: 12px; font-weight: 400; }
QMenu::item:selected { background: @primary; color: @primaryText; }
QMenu::item:disabled { color: @disabled; }
QMenu::separator { height: 1px; background: @border; margin: 4px 0; }
QMenu::indicator { width: 16px; height: 16px; }
)");
}

QString menuApplicationStyleSheet() {
    return QStringLiteral(R"(QMenu { border: %2px solid %3; border-radius: %16px; padding: %15px; }
)");
}
} // namespace choscordb::design
