#include "design_system/style/stylesheet.h"

#include "design_system/menu/menu.h"
#include "design_system/style/style_resource.h"

#include <QDebug>
#include <QHash>

#include <array>
#include <tuple>

namespace choscordb::design {
namespace {
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
    u"panel_state/panel_state_style_sheet.qss",
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

QHash<QString, QString> styleTokens(const ResolvedTheme& theme) {
    const auto& colors = theme.colors;
    QHash<QString, QString> tokens;
    for (const auto& [name, value] : colorTokens(colors))
        tokens.insert(name, cssColor(value));
    for (const auto& [name, emphasis] :
         {std::pair{"brand", Emphasis::Brand}, std::pair{"danger", Emphasis::Danger}}) {
        const auto fill = emphasisFill(colors, theme.appearance, theme.forcedContrast, emphasis);
        for (const auto& [part, color] :
             {std::pair{"ring", fill.ring}, std::pair{"bg", fill.background},
              std::pair{"start", fill.gradientStart}, std::pair{"end", fill.gradientEnd}})
            tokens.insert(QStringLiteral("button-emphasis-%1-%2")
                              .arg(QLatin1String(name), QLatin1String(part)),
                          cssColor(color));
    }
    for (const auto& [name, tint, opacity, ring] :
         {std::tuple{"success", colors.successTint, 0.2, colors.textSuccess},
          std::tuple{"warning", colors.warningTint, 0.5, colors.textWarning},
          std::tuple{"danger", colors.dangerTint, 0.5, colors.textDanger}}) {
        const auto fill = composite(withOpacity(tint, opacity), colors.base);
        tokens.insert(QStringLiteral("toast-%1-fill").arg(QLatin1String(name)), cssColor(fill));
        tokens.insert(QStringLiteral("toast-%1-ring").arg(QLatin1String(name)),
                      cssColor(composite(withOpacity(ring, 0.3), fill)));
    }
    tokens.insert(QStringLiteral("banner-secondary-text"), cssColor(secondaryBanner(colors).text));
    tokens.insert(QStringLiteral("field-focus-ring"), cssColor(withOpacity(colors.focus, 0.5)));
    tokens.insert(QStringLiteral("field-error-ring"), cssColor(withOpacity(colors.danger, 0.5)));
    tokens.insert(QStringLiteral("field-disabled-fill"),
                  cssColor(withOpacity(colors.control, 0.5)));
    tokens.insert(QStringLiteral("field-disabled-ring"), cssColor(withOpacity(colors.line, 0.5)));
    tokens.insert(QStringLiteral("shadow-margin"), pixels(detail::menuShadowMargin()));

    tokens.insert(QStringLiteral("radius-sm"), pixels(radius(Radius::Small)));
    tokens.insert(QStringLiteral("radius-md"), pixels(radius(Radius::Medium)));
    tokens.insert(QStringLiteral("radius-lg"), pixels(radius(Radius::Large)));
    tokens.insert(QStringLiteral("radius-xl"), pixels(radius(Radius::ExtraLarge)));
    tokens.insert(QStringLiteral("space-half"), pixels(spacing(Spacing::Half)));
    tokens.insert(QStringLiteral("space-1"), pixels(spacing(Spacing::One)));
    tokens.insert(QStringLiteral("space-1-5"), pixels(spacing(Spacing::OneHalf)));
    tokens.insert(QStringLiteral("space-2"), pixels(spacing(Spacing::Two)));
    tokens.insert(QStringLiteral("space-2-5"), pixels(spacing(Spacing::TwoHalf)));
    tokens.insert(QStringLiteral("space-3"), pixels(spacing(Spacing::Three)));
    tokens.insert(QStringLiteral("space-4"), pixels(spacing(Spacing::Four)));
    tokens.insert(QStringLiteral("space-6"), pixels(spacing(Spacing::Six)));
    tokens.insert(QStringLiteral("space-8"), pixels(spacing(Spacing::Eight)));
    tokens.insert(QStringLiteral("icon-size-sm"), pixels(dimension(Dimension::IconSmall)));
    tokens.insert(QStringLiteral("icon-size"), pixels(dimension(Dimension::Icon)));
    tokens.insert(QStringLiteral("select-caret-width"),
                  pixels(dimension(Dimension::Icon) + spacing(Spacing::OneHalf)));
    tokens.insert(QStringLiteral("indicator-size"), pixels(dimension(Dimension::Checkbox)));
    tokens.insert(QStringLiteral("badge-height"), pixels(dimension(Dimension::Badge) - 2));
    tokens.insert(QStringLiteral("badge-radius"), pixels(dimension(Dimension::Badge) / 2));
    tokens.insert(QStringLiteral("menu-row-height"), pixels(dimension(Dimension::MenuRow)));
    tokens.insert(QStringLiteral("control-small"), pixels(dimension(Dimension::ControlSmall)));

    tokens.insert(QStringLiteral("control-height"), pixels(dimension(Dimension::Control) - 2));
    tokens.insert(QStringLiteral("tool-height"), pixels(dimension(Dimension::ControlSmall) - 2));
    tokens.insert(QStringLiteral("pane-tab-height"),
                  pixels(dimension(Dimension::Tab) - 2 * spacing(Spacing::Half) - 2));
    tokens.insert(QStringLiteral("document-tab-height"),
                  pixels(dimension(Dimension::DocumentTab) - 2));
    tokens.insert(QStringLiteral("document-tab-width"),
                  pixels(dimension(Dimension::DocumentTabWidth)));
    tokens.insert(QStringLiteral("header-height"), pixels(dimension(Dimension::Header) - 1));
    tokens.insert(QStringLiteral("row-content-height"),
                  pixels(dimension(Dimension::Row) - 2 * spacing(Spacing::Half)));
    const int spinButton = (dimension(Dimension::Control) - 2 * spacing(Spacing::Half)) / 2;
    tokens.insert(QStringLiteral("spin-button-height"), pixels(spinButton));
    tokens.insert(QStringLiteral("spin-button-width"),
                  pixels(dimension(Dimension::IconSmall) + 2 * spacing(Spacing::One)));
    tokens.insert(QStringLiteral("spin-button-offset"), pixels(spinButton / 2));
    tokens.insert(QStringLiteral("focus-width"), pixels(focusSpec().ringWidth));
    tokens.insert(QStringLiteral("progress-height"), pixels(dimension(Dimension::Progress)));
    tokens.insert(QStringLiteral("progress-radius"), pixels(dimension(Dimension::Progress) / 2));
    tokens.insert(QStringLiteral("scrollbar-size"), pixels(dimension(Dimension::Scrollbar)));
    tokens.insert(QStringLiteral("scrollbar-radius"), pixels(dimension(Dimension::Scrollbar) / 2));
    tokens.insert(QStringLiteral("menu-width"), pixels(dimension(Dimension::MenuWidth)));
    tokens.insert(
        QStringLiteral("menu-item-width"),
        pixels(dimension(Dimension::MenuWidth) - spacing(Spacing::Two) - spacing(Spacing::Six)));
    tokens.insert(QStringLiteral("toolbar-height"), pixels(dimension(Dimension::Toolbar)));

    for (const auto& [name, style] :
         {std::pair{"caption", TextStyle::Caption}, std::pair{"callout", TextStyle::Callout},
          std::pair{"body", TextStyle::Body}, std::pair{"headline", TextStyle::Headline}}) {
        const auto spec = typographySpec(style);
        const auto prefix = QStringLiteral("font-%1").arg(QLatin1String(name));
        tokens.insert(prefix, pixels(spec.pixelSize));
        tokens.insert(prefix + QStringLiteral("-weight"), QString::number(spec.weight));
    }
    return tokens;
}
}

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
}
