#include "design_system/button/button_style.h"
#include "design_system/dialog_shell/dialog_shell_style.h"
#include "design_system/dock/dock_style.h"
#include "design_system/field/field_style.h"
#include "design_system/header/header_style.h"
#include "design_system/item_view/item_view_style.h"
#include "design_system/label/label_style.h"
#include "design_system/menu/menu_style.h"
#include "design_system/style/control_stylesheet.h"
#include "design_system/tabs/tabs_style.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region_style.h"
#include "design_system/toolbar/toolbar_style.h"
#include "design_system/tooltip/tooltip_style.h"
#include "design_system/tree/tree_style.h"

namespace choscordb::design {
QString applicationStyleSheet(const ResolvedTheme& theme, const DesignMetrics& metrics) {
    const auto& colors = theme.colors;
    // Preserve the original cascade before resolving metric placeholders.
    QString style = fieldApplicationBaseStyleSheet() + buttonApplicationBaseStyleSheet() +
                    fieldApplicationFocusStyleSheet() + itemViewApplicationStyleSheet() +
                    treeApplicationStyleSheet() + headerApplicationStyleSheet() +
                    toolbarApplicationStyleSheet() + menuApplicationStyleSheet() +
                    tooltipApplicationStyleSheet() + tabsApplicationStyleSheet() +
                    dockApplicationStyleSheet() + dialogShellApplicationStyleSheet() +
                    buttonApplicationStateStyleSheet() + labelApplicationStateStyleSheet() +
                    toastRegionApplicationStyleSheet();
    style = style.arg(metrics.controlHeight)
                .arg(metrics.separatorWidth)
                .arg(colors.separator.name())
                .arg(metrics.controlRadius)
                .arg(metrics.spacingMedium)
                .arg(colors.surface.name())
                .arg(colors.focus.name())
                .arg(colors.actionText.name())
                .arg(colors.action.name())
                .arg(colors.actionHover.name())
                .arg(colors.actionPressed.name())
                .arg(colors.selection.name())
                .arg(metrics.navigationRowHeight)
                .arg(metrics.workspaceChromeHeight)
                .arg(metrics.spacingSmall)
                .arg(metrics.popoverRadius)
                .arg(colors.text.name())
                .arg(colors.elevatedSurface.name())
                .arg(colors.success.name())
                .arg(colors.warning.name())
                .arg(colors.danger.name())
                .arg(metrics.dialogRadius)
                .arg(colors.disabled.name())
                .arg(colors.mutedText.name());
    return style + controlStyleSheet(theme) + buttonPaintedStyleSheet();
}

} // namespace choscordb::design
