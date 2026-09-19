#include "design_system/toast_region/toast_region_style.h"
#include "design_system/style/style_resource.h"
#include "design_system/theme.h"

namespace choscordb::design {

QString toastRegionApplicationStyleSheet(const ResolvedTheme& theme) {
    const bool dark = theme.appearance == ResolvedAppearance::Dark;
    QString sheet =
        loadStyleSheet(QStringLiteral("toast_region/toast_region_application_style_sheet.qss"));
    sheet.replace(QStringLiteral("@successSurface"),
                  dark ? QStringLiteral("#283e34") : QStringLiteral("#eaf4ef"));
    sheet.replace(QStringLiteral("@warningSurface"),
                  dark ? QStringLiteral("#473b22") : QStringLiteral("#fff3d6"));
    sheet.replace(QStringLiteral("@dangerSurface"),
                  dark ? QStringLiteral("#492d2b") : QStringLiteral("#fdecea"));
    return sheet;
}
} // namespace choscordb::design
