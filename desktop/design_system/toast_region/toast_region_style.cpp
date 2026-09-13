#include "design_system/toast_region/toast_region_style.h"

namespace choscordb::design {

QString toastRegionApplicationStyleSheet() {
    return QStringLiteral(
        R"(QLabel#toastRegion { background-color: %18; border: %2px solid %3; padding: %5px; }
)");
}
} // namespace choscordb::design
