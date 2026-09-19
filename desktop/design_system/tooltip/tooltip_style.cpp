#include "design_system/tooltip/tooltip_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString tooltipStyleSheet() {
    return loadStyleSheet(QStringLiteral("tooltip/tooltip_style_sheet.qss"));
}

QString tooltipApplicationStyleSheet() {
    return loadStyleSheet(QStringLiteral("tooltip/tooltip_application_style_sheet.qss"));
}
} // namespace choscordb::design
