#include "design_system/tabs/tabs_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString tabsStyleSheet() {
    return loadStyleSheet(QStringLiteral("tabs/tabs_style_sheet.qss"));
}

QString tabsApplicationStyleSheet() {
    return loadStyleSheet(QStringLiteral("tabs/tabs_application_style_sheet.qss"));
}
} // namespace choscordb::design
