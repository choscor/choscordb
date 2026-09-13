#include "design_system/label/label_style.h"

namespace choscordb::design {
QString labelStyleSheet() {
    return QStringLiteral(
        R"(QLabel[designRole="heading"] { font-size: 14px; font-weight: 600; color: @foreground; }
QLabel[designRole="description"] { color: @mutedText; }
)");
}

QString labelApplicationStateStyleSheet() {
    return QStringLiteral(R"(QLabel[state="completed"], QLabel[state="success"] { color: %19; }
QLabel[state="warning"] { color: %20; }
QLabel[state="failed"], QLabel[state="error"] { color: %21; }
)");
}
} // namespace choscordb::design
