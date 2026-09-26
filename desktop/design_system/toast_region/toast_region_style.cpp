#include "design_system/toast_region/toast_region_style.h"
#include "design_system/style/style_resource.h"
#include "design_system/theme.h"

namespace choscordb::design {

QString toastRegionApplicationStyleSheet(const ResolvedTheme& theme) {
    QString sheet =
        loadStyleSheet(QStringLiteral("toast_region/toast_region_application_style_sheet.qss"));
    sheet.replace(QStringLiteral("@successSurface"), theme.colors.successSurface.name());
    sheet.replace(QStringLiteral("@warningSurface"), theme.colors.warningSurface.name());
    sheet.replace(QStringLiteral("@dangerSurface"), theme.colors.dangerSurface.name());
    return sheet;
}
} // namespace choscordb::design
