#include "design_system/spin_box/spin_box_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString spinBoxStyleSheet() {
    return loadStyleSheet(QStringLiteral("spin_box/spin_box_style_sheet.qss"));
}
} // namespace choscordb::design
