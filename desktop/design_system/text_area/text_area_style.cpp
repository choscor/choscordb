#include "design_system/text_area/text_area_style.h"

namespace choscordb::design {
QString textAreaStyleSheet() {
    return QStringLiteral(
        R"(QPlainTextEdit, QTextEdit { border: 1px solid @input; border-radius: @controlRadius; padding: 8px; background: @field; color: @foreground; }
)");
}
} // namespace choscordb::design
