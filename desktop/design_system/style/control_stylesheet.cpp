#include "design_system/style/control_stylesheet.h"

#include "design_system/badge/badge_style.h"
#include "design_system/button/button_style.h"
#include "design_system/field/field_style.h"
#include "design_system/header/header_style.h"
#include "design_system/item_view/item_view_style.h"
#include "design_system/kbd/kbd_style.h"
#include "design_system/label/label_style.h"
#include "design_system/menu/menu_style.h"
#include "design_system/progress/progress_style.h"
#include "design_system/scrollbar/scrollbar_style.h"
#include "design_system/select/select_style.h"
#include "design_system/separator/separator_style.h"
#include "design_system/spin_box/spin_box_style.h"
#include "design_system/splitter/splitter_style.h"
#include "design_system/tabs/tabs_style.h"
#include "design_system/text_area/text_area_style.h"
#include "design_system/tool_button/tool_button_style.h"
#include "design_system/toolbar/toolbar_style.h"
#include "design_system/tooltip/tooltip_style.h"

#include "design_system/list/list_style.h"
#include "design_system/menu/menu.h"
#include "design_system/table/table_style.h"
#include "design_system/tree/tree_style.h"

namespace choscordb::design {
QString controlStyleSheet(const ResolvedTheme& theme) {
    const auto cssColor = [](const QColor& color) {
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(color.alpha());
    };
    // Preserve cascade order across component rules.
    QString sheet =
        buttonStyleSheet() + fieldSelectionStyleSheet() + fieldCaptionStyleSheet() +
        labelStyleSheet() + kbdStyleSheet() + badgeStyleSheet() + progressStyleSheet() +
        tooltipStyleSheet() + splitterStyleSheet() + separatorStyleSheet() + tabsStyleSheet() +
        toolButtonStyleSheet() + toolbarStyleSheet() + itemViewStyleSheet() + tableStyleSheet() +
        treeStyleSheet() + tableItemStyleSheet() + listStyleSheet() + treeItemStyleSheet() +
        itemViewStateStyleSheet() + treeStateStyleSheet() + headerStyleSheet() + menuStyleSheet() +
        scrollbarStyleSheet() + fieldBaseStyleSheet() + textAreaStyleSheet() +
        fieldStateStyleSheet() + selectStyleSheet() + spinBoxStyleSheet();
    auto field = theme.colors.surface;
    auto destructiveTint = theme.colors.destructive;
    destructiveTint.setAlphaF(theme.appearance == ResolvedAppearance::Dark ? 0.2 : 0.1);
    auto destructiveHover = theme.colors.destructive;
    destructiveHover.setAlphaF(theme.appearance == ResolvedAppearance::Dark ? 0.3 : 0.2);
    const int shadowMargin = detail::menuShadowMargin();
    const auto pixels = [&sheet](const char* name, int value) {
        sheet.replace(QLatin1String(name), QString::number(value) + "px");
    };
    pixels("@controlRadius", radius(Radius::Large));
    pixels("@popoverRadius", radius(Radius::TwoExtraLarge));
    pixels("@buttonHeight", dimension(Dimension::Button) - 2);
    pixels("@inputHeight", dimension(Dimension::Input) - 2);
    pixels("@selectorHeight", dimension(Dimension::Selector) - 2);
    pixels("@toolHeight", dimension(Dimension::ButtonSmall) - 2);
    pixels("@paneTabHeight", dimension(Dimension::PaneTab) - 3);
    pixels("@documentTabHeight", dimension(Dimension::DocumentTab) - 2);
    pixels("@tableHeaderHeight", dimension(Dimension::TableHeader) - 1);
    pixels("@navigationLineHeight", dimension(Dimension::NavigationRow) - 11);
    pixels("@progressHeight", dimension(Dimension::Progress));
    sheet.replace("@shadowMargin", QString::number(shadowMargin) + "px");
    sheet.replace("@destructiveTint", cssColor(destructiveTint));
    sheet.replace("@destructiveHover", cssColor(destructiveHover));
    sheet.replace("@primaryText", cssColor(theme.colors.primaryForeground));
    sheet.replace("@primary", cssColor(theme.colors.primary));
    sheet.replace("@background", cssColor(theme.colors.background));
    sheet.replace("@mutedText", cssColor(theme.colors.mutedText));
    sheet.replace("@muted", cssColor(theme.colors.muted));
    sheet.replace("@input", cssColor(theme.colors.input));
    sheet.replace("@foreground", cssColor(theme.colors.foreground));
    sheet.replace("@field", cssColor(field));
    sheet.replace("@sidebar", cssColor(theme.colors.sidebar));
    sheet.replace("@focus", cssColor(theme.colors.focus));
    sheet.replace("@dangerText", cssColor(theme.colors.danger));
    sheet.replace("@destructive", cssColor(theme.colors.destructive));
    sheet.replace("@disabled", cssColor(theme.colors.disabled));
    sheet.replace("@popoverText", cssColor(theme.colors.popoverForeground));
    sheet.replace("@popover", cssColor(theme.colors.popover));
    sheet.replace("@border", cssColor(theme.colors.border));
    sheet.replace("@accentText", cssColor(theme.colors.accentForeground));
    sheet.replace("@navigationText", cssColor(theme.colors.sidebarAccentForeground));
    sheet.replace("@accent", cssColor(theme.colors.accent));
    return sheet;
}
} // namespace choscordb::design
