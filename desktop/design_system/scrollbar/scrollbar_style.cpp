#include "design_system/scrollbar/scrollbar_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString scrollbarStyleSheet() {
    return loadStyleSheet(QStringLiteral("scrollbar/scrollbar_style_sheet.qss"));
}
} // namespace choscordb::design
