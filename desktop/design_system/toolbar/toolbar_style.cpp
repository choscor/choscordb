#include "design_system/toolbar/toolbar_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString toolbarStyleSheet() {
    return loadStyleSheet(QStringLiteral("toolbar/toolbar_style_sheet.qss"));
}

QString toolbarApplicationStyleSheet() {
    return loadStyleSheet(QStringLiteral("toolbar/toolbar_application_style_sheet.qss"));
}
} // namespace choscordb::design
