#include "design_system/toast_region/toast_region_style.h"
#include "design_system/theme.h"

namespace choscordb::design {

QString toastRegionApplicationStyleSheet(const ResolvedTheme& theme) {
    const bool dark = theme.appearance == ResolvedAppearance::Dark;
    QString sheet = QStringLiteral(
        R"(QLabel#toastRegion { background-color: %18; color: %17; border: %2px solid %3; border-radius: %16px; padding: %5px; }
QLabel#toastRegion[variant="success"] { background-color: @successSurface; border-left: 4px solid %19; }
QLabel#toastRegion[variant="warning"] { background-color: @warningSurface; border-left: 4px solid %20; }
QLabel#toastRegion[variant="danger"] { background-color: @dangerSurface; border-left: 4px solid %21; }
)");
    sheet.replace(QStringLiteral("@successSurface"),
                  dark ? QStringLiteral("#283e34") : QStringLiteral("#eaf4ef"));
    sheet.replace(QStringLiteral("@warningSurface"),
                  dark ? QStringLiteral("#473b22") : QStringLiteral("#fff3d6"));
    sheet.replace(QStringLiteral("@dangerSurface"),
                  dark ? QStringLiteral("#492d2b") : QStringLiteral("#fdecea"));
    return sheet;
}
} // namespace choscordb::design
