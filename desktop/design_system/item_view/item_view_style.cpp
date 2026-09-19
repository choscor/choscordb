#include "design_system/item_view/item_view_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString itemViewStyleSheet() {
    return loadStyleSheet(QStringLiteral("item_view/item_view_style_sheet.qss"));
}

QString itemViewStateStyleSheet() {
    return loadStyleSheet(QStringLiteral("item_view/item_view_state_style_sheet.qss"));
}

QString itemViewApplicationStyleSheet() {
    return loadStyleSheet(QStringLiteral("item_view/item_view_application_style_sheet.qss"));
}
} // namespace choscordb::design
