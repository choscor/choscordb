#include "design_system/label/label_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString labelStyleSheet() {
    return loadStyleSheet(QStringLiteral("label/label_style_sheet.qss"));
}

QString labelApplicationStateStyleSheet() {
    return loadStyleSheet(QStringLiteral("label/label_application_state_style_sheet.qss"));
}
} // namespace choscordb::design
