#include "design_system/tree/tree_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString treeStyleSheet() {
    return loadStyleSheet(QStringLiteral("tree/tree_style_sheet.qss"));
}

QString treeItemStyleSheet() {
    return loadStyleSheet(QStringLiteral("tree/tree_item_style_sheet.qss"));
}

QString treeStateStyleSheet() {
    return loadStyleSheet(QStringLiteral("tree/tree_state_style_sheet.qss"));
}

QString treeApplicationStyleSheet() {
    return loadStyleSheet(QStringLiteral("tree/tree_application_style_sheet.qss"));
}
} // namespace choscordb::design
