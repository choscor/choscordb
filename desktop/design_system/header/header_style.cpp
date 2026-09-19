#include "design_system/header/header_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString headerStyleSheet() {
    return loadStyleSheet(QStringLiteral("header/header_style_sheet.qss"));
}

QString headerApplicationStyleSheet() {
    return loadStyleSheet(QStringLiteral("header/header_application_style_sheet.qss"));
}
} // namespace choscordb::design
