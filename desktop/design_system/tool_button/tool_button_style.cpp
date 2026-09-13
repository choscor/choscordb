#include "design_system/tool_button/tool_button_style.h"

namespace choscordb::design {
QString toolButtonStyleSheet() {
    return QStringLiteral(
        R"(QToolButton { icon-size: 16px; min-height: @toolHeight; max-height: @toolHeight; border: 1px solid transparent; border-radius: @controlRadius; padding: 0 9px; font-size: 11px; background: transparent; color: @foreground; }
QToolButton[iconOnly="true"] { min-width: 30px; max-width: 30px; padding: 0; }
QToolButton:hover, QToolButton:checked { background: @muted; color: @foreground; }
QToolButton:pressed { background: @accent; }
QToolButton:focus { border-color: @focus; }
QToolButton:disabled { color: @disabled; }
QToolButton::menu-indicator { width: 12px; height: 12px; subcontrol-position: right center; }
)");
}
} // namespace choscordb::design
