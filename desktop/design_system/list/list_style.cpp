#include "design_system/list/list_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString listStyleSheet() {
    return loadStyleSheet(QStringLiteral("list/list_style_sheet.qss"));
}
} // namespace choscordb::design
