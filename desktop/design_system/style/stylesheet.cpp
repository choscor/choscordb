#include "design_system/style/stylesheet.h"

#include "design_system/menu/menu.h"
#include "design_system/style/style_resource.h"

#include <QDebug>
#include <QHash>

#include <array>

namespace choscordb::design {
namespace {
// The shared cascade, in order: a later rule wins over an earlier rule of
// equal specificity. Each selector and property pair is defined in one file.
constexpr std::array sharedCascade = {
    u"dock/dock_title_style_sheet.qss",
    u"dialog_shell/dialog_shell_style_sheet.qss",
    u"dialog_sections/dialog_sections_style_sheet.qss",
    u"toast_region/toast_region_style_sheet.qss",
    u"button/button_style_sheet.qss",
    u"field/field_selection_style_sheet.qss",
    u"field/field_caption_style_sheet.qss",
    u"label/label_style_sheet.qss",
    u"kbd/kbd_style_sheet.qss",
    u"badge/badge_style_sheet.qss",
    u"progress/progress_style_sheet.qss",
    u"tooltip/tooltip_style_sheet.qss",
    u"splitter/splitter_style_sheet.qss",
    u"separator/separator_style_sheet.qss",
    u"tabs/tabs_style_sheet.qss",
    u"tool_button/tool_button_style_sheet.qss",
    u"toolbar/toolbar_style_sheet.qss",
    u"item_view/item_view_style_sheet.qss",
    u"table/table_style_sheet.qss",
    u"tree/tree_style_sheet.qss",
    u"table/table_item_style_sheet.qss",
    u"list/list_style_sheet.qss",
    u"tree/tree_item_style_sheet.qss",
    u"item_view/item_view_state_style_sheet.qss",
    u"tree/tree_state_style_sheet.qss",
    u"header/header_style_sheet.qss",
    u"menu/menu_style_sheet.qss",
    u"scrollbar/scrollbar_style_sheet.qss",
    u"quick_search/quick_search_style_sheet.qss",
    u"field/field_base_style_sheet.qss",
    u"text_area/text_area_style_sheet.qss",
    u"field/field_state_style_sheet.qss",
    u"select/select_style_sheet.qss",
    u"spin_box/spin_box_style_sheet.qss",
    u"button/button_painted_style_sheet.qss",
    u"status_line/status_line_style_sheet.qss",
};

QString cssColor(const QColor& color) {
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QString pixels(int value) {
    return QString::number(value) + QStringLiteral("px");
}

QColor withAlpha(QColor color, double alpha) {
    color.setAlphaF(alpha);
    return color;
}

QHash<QString, QString> styleTokens(const ResolvedTheme& theme) {
    const auto& colors = theme.colors;
    const bool dark = theme.appearance == ResolvedAppearance::Dark;
    QHash<QString, QString> tokens;
    for (const auto& [name, value] : colorTokens(colors))
        tokens.insert(name, cssColor(value));
    tokens.insert(QStringLiteral("p1-radius-2"), pixels(2));
    tokens.insert(QStringLiteral("p1-radius-5"), pixels(radius(Radius::Medium)));
    tokens.insert(QStringLiteral("p1-radius-6"), pixels(6));
    tokens.insert(QStringLiteral("p1-radius-8"), pixels(radius(Radius::ExtraLarge)));

    // Destructive fills tint `danger`; hover adds 10%.
    tokens.insert(QStringLiteral("destructive-tint"),
                  cssColor(withAlpha(colors.danger, dark ? 0.2 : 0.1)));
    tokens.insert(QStringLiteral("destructive-hover"),
                  cssColor(withAlpha(colors.danger, dark ? 0.3 : 0.2)));
    tokens.insert(QStringLiteral("sidebar-glass-top"),
                  cssColor(withAlpha(colors.sidebar, 232 / 255.0)));
    tokens.insert(QStringLiteral("sidebar-glass-bottom"),
                  cssColor(withAlpha(colors.surfaceRaised, 208 / 255.0)));
    tokens.insert(QStringLiteral("shadow-margin"), pixels(detail::menuShadowMargin()));

    tokens.insert(QStringLiteral("radius-sm"), pixels(radius(Radius::Small)));
    tokens.insert(QStringLiteral("radius-md"), pixels(radius(Radius::Large)));
    tokens.insert(QStringLiteral("radius-lg"), pixels(radius(Radius::TwoExtraLarge)));
    tokens.insert(QStringLiteral("scrollbar-radius"), pixels(5));
    tokens.insert(QStringLiteral("space-1"), pixels(spacing(Spacing::One)));
    tokens.insert(QStringLiteral("space-2"), pixels(spacing(Spacing::Two)));

    // Stylesheet min-heights exclude the 1px borders that Qt adds around them.
    tokens.insert(QStringLiteral("button-height"), pixels(dimension(Dimension::Button) - 2));
    tokens.insert(QStringLiteral("field-height"), pixels(dimension(Dimension::Input) - 2));
    tokens.insert(QStringLiteral("select-height"), pixels(dimension(Dimension::Selector) - 2));
    tokens.insert(QStringLiteral("tool-height"), pixels(dimension(Dimension::ButtonSmall) - 2));
    tokens.insert(QStringLiteral("pane-tab-height"), pixels(dimension(Dimension::PaneTab) - 3));
    tokens.insert(QStringLiteral("document-tab-height"),
                  pixels(dimension(Dimension::DocumentTab) - 2));
    tokens.insert(QStringLiteral("header-height"), pixels(dimension(Dimension::TableHeader) - 1));
    tokens.insert(QStringLiteral("row-line-height"),
                  pixels(dimension(Dimension::NavigationRow) - 11));
    tokens.insert(QStringLiteral("nav-item-min-height"), pixels(18));
    tokens.insert(QStringLiteral("nav-item-padding"), pixels(spacing(Spacing::Half)));
    tokens.insert(QStringLiteral("progress-height"), pixels(dimension(Dimension::Progress)));
    tokens.insert(QStringLiteral("toolbar-height"), pixels(35));

    tokens.insert(QStringLiteral("font-caption"),
                  pixels(typographySpec(TypographyRole::SectionCaption).pixelSize));
    tokens.insert(QStringLiteral("font-small"),
                  pixels(typographySpec(TypographyRole::Small).pixelSize));
    tokens.insert(QStringLiteral("font-dense"),
                  pixels(typographySpec(TypographyRole::Field).pixelSize));
    tokens.insert(QStringLiteral("font-title"),
                  pixels(typographySpec(TypographyRole::Heading).pixelSize));
    return tokens;
}
} // namespace

QString resolveStyleTokens(QString sheet, const ResolvedTheme& theme) {
    const auto tokens = styleTokens(theme);
    const auto isNameChar = [](QChar c) {
        return (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-';
    };
    QString resolved;
    resolved.reserve(sheet.size());
    qsizetype index = 0;
    while (index < sheet.size()) {
        const auto at = sheet.indexOf(u'@', index);
        if (at < 0)
            break;
        auto end = at + 1;
        while (end < sheet.size() && isNameChar(sheet.at(end)))
            ++end;
        const auto name = QStringView(sheet).sliced(at + 1, end - at - 1);
        const auto value = tokens.constFind(name.toString());
        resolved += QStringView(sheet).sliced(index, at - index);
        if (value == tokens.cend()) {
            qWarning() << "Unknown stylesheet token" << name;
            resolved += QStringView(sheet).sliced(at, end - at);
        } else {
            resolved += *value;
        }
        index = end;
    }
    resolved += QStringView(sheet).sliced(index);
    return resolved;
}

QString themedStyleSheet(QStringView relativePath, const ResolvedTheme& theme) {
    return resolveStyleTokens(loadStyleSheet(relativePath), theme);
}

QString applicationStyleSheet(const ResolvedTheme& theme) {
    QString sheet;
    for (const auto path : sharedCascade)
        sheet += loadStyleSheet(path);
    return resolveStyleTokens(std::move(sheet), theme);
}
} // namespace choscordb::design
