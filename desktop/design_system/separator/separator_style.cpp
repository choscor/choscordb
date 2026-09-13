#include "design_system/separator/separator_style.h"

namespace choscordb::design {
QString separatorStyleSheet() {
    return QStringLiteral(
        R"(QFrame[frameShape="4"] { max-height: 1px; border: 0; background: @border; }
QFrame[frameShape="5"] { max-width: 1px; border: 0; background: @border; }
)");
}
} // namespace choscordb::design
