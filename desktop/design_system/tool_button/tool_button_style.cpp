#include "design_system/tool_button/tool_button_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString toolButtonStyleSheet() {
    return loadStyleSheet(QStringLiteral("tool_button/tool_button_style_sheet.qss"));
}
} // namespace choscordb::design
