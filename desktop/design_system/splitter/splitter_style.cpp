#include "design_system/splitter/splitter_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString splitterStyleSheet() {
    return loadStyleSheet(QStringLiteral("splitter/splitter_style_sheet.qss"));
}
} // namespace choscordb::design
