#include "design_system/kbd/kbd_style.h"

namespace choscordb::design {
QString kbdStyleSheet() {
    return QStringLiteral(
        R"(QLabel[designRole="kbd"] { background: @muted; color: @mutedText; border-radius: 4px; padding: 2px 5px; font-size: 10px; }
)");
}
} // namespace choscordb::design
