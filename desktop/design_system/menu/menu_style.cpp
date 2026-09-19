#include "design_system/menu/menu_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString menuStyleSheet() {
    return loadStyleSheet(QStringLiteral("menu/menu_style_sheet.qss"));
}

QString menuApplicationStyleSheet() {
    return loadStyleSheet(QStringLiteral("menu/menu_application_style_sheet.qss"));
}
} // namespace choscordb::design
