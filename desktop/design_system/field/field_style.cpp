#include "design_system/field/field_style.h"

namespace choscordb::design {
QString fieldSelectionStyleSheet() {
    return QStringLiteral(
        R"(QLineEdit, QPlainTextEdit, QTextEdit { selection-background-color: @primary; selection-color: @primaryText; }
)");
}

QString fieldCaptionStyleSheet() {
    return QStringLiteral(R"(QLabel, QCheckBox, QRadioButton, QGroupBox { color: @foreground; }
)");
}

QString fieldBaseStyleSheet() {
    return QStringLiteral(R"(QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QKeySequenceEdit {
  min-height: @inputHeight; padding: 0 9px; border: 1px solid @input;
  border-radius: @controlRadius; color: @foreground; background: @field;
}
)");
}

QString fieldStateStyleSheet() {
    return QStringLiteral(R"(QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus,
QPlainTextEdit:focus, QTextEdit:focus, QKeySequenceEdit:focus { border-color: @focus; }
QLineEdit[invalid="true"], QComboBox[invalid="true"], QSpinBox[invalid="true"],
QDoubleSpinBox[invalid="true"], QPlainTextEdit[invalid="true"], QTextEdit[invalid="true"],
QKeySequenceEdit[invalid="true"] { border-color: @destructive; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled,
QPlainTextEdit:disabled, QTextEdit:disabled { color: @disabled; }
)");
}

QString fieldApplicationBaseStyleSheet() {
    return QStringLiteral(R"(
QPushButton, QToolButton, QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
  min-height: %1px;
  border: %2px solid %3;
  border-radius: %4px;
}
QPushButton, QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
  padding-left: %5px;
  padding-right: %5px;
}
)");
}

QString fieldApplicationFocusStyleSheet() {
    return QStringLiteral(R"(QPushButton:focus, QToolButton:focus, QLineEdit:focus, QComboBox:focus,
QSpinBox:focus, QDoubleSpinBox:focus {
  border: %2px solid %7;
}
)");
}
} // namespace choscordb::design
