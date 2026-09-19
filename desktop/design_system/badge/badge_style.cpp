#include "design_system/badge/badge_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString badgeStyleSheet() {
    return loadStyleSheet(QStringLiteral("badge/badge_style_sheet.qss"));
}
} // namespace choscordb::design
