#include "design_system/select/select_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString selectStyleSheet() {
    return loadStyleSheet(QStringLiteral("select/select_style_sheet.qss"));
}
} // namespace choscordb::design
