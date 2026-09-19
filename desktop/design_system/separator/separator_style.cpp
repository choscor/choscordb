#include "design_system/separator/separator_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString separatorStyleSheet() {
    return loadStyleSheet(QStringLiteral("separator/separator_style_sheet.qss"));
}
} // namespace choscordb::design
