#include "design_system/progress/progress_style.h"

namespace choscordb::design {
QString progressStyleSheet() {
    return QStringLiteral(
        R"(QProgressBar { min-height: @progressHeight; max-height: @progressHeight; border: 0; border-radius: 2px; background: @muted; }
QProgressBar::chunk { border-radius: 2px; background: @primary; }
)");
}
} // namespace choscordb::design
