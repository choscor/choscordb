#include "design_system/progress/progress_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString progressStyleSheet() {
    return loadStyleSheet(QStringLiteral("progress/progress_style_sheet.qss"));
}
} // namespace choscordb::design
