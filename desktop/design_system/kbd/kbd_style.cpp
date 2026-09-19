#include "design_system/kbd/kbd_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString kbdStyleSheet() {
    return loadStyleSheet(QStringLiteral("kbd/kbd_style_sheet.qss"));
}
} // namespace choscordb::design
