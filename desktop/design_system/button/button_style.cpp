#include "design_system/button/button_style.h"

namespace choscordb::design {
QString buttonStyleSheet() {
    return QStringLiteral(R"(
QPushButton { min-height: @buttonHeight; padding: 0 12px; border: 1px solid @border; border-radius: @controlRadius; background: @field; color: @foreground; font-weight: 400; }
QPushButton[variant="default"] { background: @primary; color: @primaryText; border-color: transparent; font-weight: 400; }
QPushButton[variant="outline"] { background: @field; color: @foreground; border-color: @border; font-weight: 400; }
QPushButton[variant="outline"]:hover, QPushButton[variant="outline"]:pressed { background: @muted; color: @foreground; }
QPushButton[variant="destructive"] { background: @destructiveTint; color: @dangerText; border-color: transparent; font-weight: 400; }
QPushButton[variant="destructive"]:hover, QPushButton[variant="destructive"]:pressed { background: @destructiveHover; color: @dangerText; }
QPushButton:default:focus, QPushButton:checked:focus, QPushButton[variant="default"]:focus,
QPushButton[primary="true"]:focus { border-color: @primaryText; }
QPushButton[variant="outline"]:focus, QPushButton[variant="destructive"]:focus { border-color: @focus; }
QPushButton[variant="default"]:disabled, QPushButton[variant="outline"]:disabled, QPushButton[variant="destructive"]:disabled { color: @disabled; }
)");
}

QString buttonApplicationBaseStyleSheet() {
    return QStringLiteral(R"(QPushButton, QToolButton { background-color: %6; }
QPushButton:hover { border-color: %7; }
QPushButton:default, QPushButton:checked {
  color: %8;
  background-color: %9;
  border-color: %9;
}
QPushButton:default:hover, QPushButton:checked:hover {
  background-color: %10;
  border-color: %10;
}
QPushButton:default:pressed, QPushButton:checked:pressed {
  background-color: %11;
  border-color: %11;
}
)");
}

QString buttonApplicationStateStyleSheet() {
    return QStringLiteral(R"(QPushButton[primary="true"], QToolButton[primary="true"] {
  color: %8; background-color: %9; border-color: %9;
}
QPushButton:pressed {
  background-color: %11; border-color: %11; color: %8;
}
QPushButton:disabled, QToolButton:disabled { color: %23; }
)");
}

QString buttonPaintedStyleSheet() {
    return QStringLiteral(
        "QPushButton[designButton=\"true\"] { min-height: 0; padding: 0; border: 0; }");
}
} // namespace choscordb::design
