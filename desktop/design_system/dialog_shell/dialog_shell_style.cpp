#include "design_system/dialog_shell/dialog_shell_style.h"

namespace choscordb::design {

QString dialogShellApplicationStyleSheet() {
    return QStringLiteral(R"(QDialog[appDialog="true"] { border-radius: %22px; }
QDialog[appDialog="true"] QLabel[dialogDescription="true"] { color: %24; }
QDialog[appDialog="true"] QLabel[dialogStatus="true"] {
  border: %2px solid %3; border-radius: %4px; padding: %15px;
}
)");
}
} // namespace choscordb::design
