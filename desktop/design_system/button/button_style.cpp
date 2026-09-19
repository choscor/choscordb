#include "design_system/button/button_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString buttonStyleSheet() {
    return loadStyleSheet(QStringLiteral("button/button_style_sheet.qss"));
}

QString buttonApplicationBaseStyleSheet() {
    return loadStyleSheet(QStringLiteral("button/button_application_base_style_sheet.qss"));
}

QString buttonApplicationStateStyleSheet() {
    return loadStyleSheet(QStringLiteral("button/button_application_state_style_sheet.qss"));
}

QString buttonPaintedStyleSheet() {
    return loadStyleSheet(QStringLiteral("button/button_painted_style_sheet.qss"));
}
} // namespace choscordb::design
