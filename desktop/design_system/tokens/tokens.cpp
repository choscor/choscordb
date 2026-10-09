#include "design_system/theme.h"

#include <utility>

namespace choscordb::design {
QList<DesignToken> designTokens(ResolvedAppearance appearance) {
    const auto colors = resolveColors(appearance);
    QList<DesignToken> tokens;
    const auto add = [&tokens](const QString& name, const QString& value) {
        tokens.append({name, value, QStringLiteral("desktop/design_system/tokens/tokens.cpp")});
    };
    const auto color = [&add](const QString& name, const QColor& value) {
        add(QStringLiteral("color.") + name,
            value.alpha() == 255 ? value.name() : value.name(QColor::HexArgb));
    };
    for (const auto& [name, value] : colorTokens(colors))
        color(name, value);
    add(QStringLiteral("backdrop.blur"), QString::number(backdropBlurRadius()) + "px");

    const auto pixels = [&add](const QString& name, int value) {
        add(name, QString::number(value) + QStringLiteral("px"));
    };
    pixels(QStringLiteral("spacing.0.25"), spacing(Spacing::Quarter));
    pixels(QStringLiteral("spacing.0.5"), spacing(Spacing::Half));
    pixels(QStringLiteral("spacing.1"), spacing(Spacing::One));
    pixels(QStringLiteral("spacing.1.5"), spacing(Spacing::OneHalf));
    pixels(QStringLiteral("spacing.2"), spacing(Spacing::Two));
    pixels(QStringLiteral("spacing.2.5"), spacing(Spacing::TwoHalf));
    pixels(QStringLiteral("spacing.3"), spacing(Spacing::Three));
    pixels(QStringLiteral("spacing.4"), spacing(Spacing::Four));
    pixels(QStringLiteral("spacing.6"), spacing(Spacing::Six));
    pixels(QStringLiteral("spacing.8"), spacing(Spacing::Eight));
    pixels(QStringLiteral("radius.sm"), radius(Radius::Small));
    pixels(QStringLiteral("radius.md"), radius(Radius::Medium));
    pixels(QStringLiteral("radius.lg"), radius(Radius::Large));
    pixels(QStringLiteral("radius.xl"), radius(Radius::ExtraLarge));
    pixels(QStringLiteral("radius.2xl"), radius(Radius::TwoExtraLarge));
    pixels(QStringLiteral("size.button.xs"), dimension(Dimension::ButtonExtraSmall));
    pixels(QStringLiteral("size.button.sm"), dimension(Dimension::ButtonSmall));
    pixels(QStringLiteral("size.button.default"), dimension(Dimension::Button));
    pixels(QStringLiteral("size.button.lg"), dimension(Dimension::ButtonLarge));
    pixels(QStringLiteral("size.input"), dimension(Dimension::Input));
    pixels(QStringLiteral("size.selector"), dimension(Dimension::Selector));
    pixels(QStringLiteral("size.checkbox"), dimension(Dimension::Checkbox));
    pixels(QStringLiteral("size.icon.small"), dimension(Dimension::IconSmall));
    pixels(QStringLiteral("size.icon.default"), dimension(Dimension::Icon));
    pixels(QStringLiteral("size.icon.large"), dimension(Dimension::IconLarge));
    pixels(QStringLiteral("size.modal.width"), dimension(Dimension::ModalWidth));
    pixels(QStringLiteral("size.completion.width"), dimension(Dimension::CompletionPopupWidth));
    pixels(QStringLiteral("size.table.row"), dimension(Dimension::TableRow));
    pixels(QStringLiteral("size.table.column"), dimension(Dimension::TableColumn));
    pixels(QStringLiteral("size.table.header"), dimension(Dimension::TableHeader));
    pixels(QStringLiteral("size.navigation.row"), dimension(Dimension::NavigationRow));
    pixels(QStringLiteral("size.tab.pane"), dimension(Dimension::PaneTab));
    pixels(QStringLiteral("size.tab.document"), dimension(Dimension::DocumentTab));
    pixels(QStringLiteral("size.toast.progress"), dimension(Dimension::ToastProgress));
    add(QStringLiteral("icon.stroke"), QString::number(iconStrokeWidth()) + "px");

    pixels(QStringLiteral("border.width"), focusSpec().borderWidth);
    pixels(QStringLiteral("focus.width"), focusSpec().ringWidth);
    add(QStringLiteral("focus.referenceOpacity"),
        QString::number(focusSpec().referenceRingOpacity));
    for (const auto& [name, role] :
         {std::pair{"ui", TypographyRole::Ui}, std::pair{"small", TypographyRole::Small},
          std::pair{"heading", TypographyRole::Heading}, std::pair{"base", TypographyRole::Base},
          std::pair{"dialogTitle", TypographyRole::DialogTitle},
          std::pair{"monospace", TypographyRole::Monospace},
          std::pair{"field", TypographyRole::Field},
          std::pair{"sectionCaption", TypographyRole::SectionCaption},
          std::pair{"metadata", TypographyRole::Metadata}}) {
        const auto spec = typographySpec(role);
        const auto prefix = QStringLiteral("typography.%1.").arg(QLatin1String(name));
        add(prefix + QStringLiteral("family"), spec.family);
        pixels(prefix + QStringLiteral("size"), spec.pixelSize);
        pixels(prefix + QStringLiteral("lineHeight"), spec.lineHeight);
        add(prefix + QStringLiteral("weight"), QString::number(spec.weight));
    }
    for (const auto& [name, role] :
         {std::pair{"interaction", Motion::Interaction}, std::pair{"popup", Motion::Popup}}) {
        const auto spec = motionSpec(role);
        const auto prefix = QStringLiteral("motion.%1").arg(QLatin1String(name));
        add(prefix, QString::number(spec.durationMs) + QStringLiteral("ms"));
        add(prefix + QStringLiteral(".easing"), spec.easing);
    }
    for (const auto& [name, role] :
         {std::pair{"popover", Elevation::Popover}, std::pair{"dialog", Elevation::Dialog}}) {
        const auto layers = elevation(role);
        for (qsizetype i = 0; i < layers.size(); ++i) {
            const auto prefix = QStringLiteral("elevation.%1%2.")
                                    .arg(QLatin1String(name))
                                    .arg(i == 0 ? QString{} : QStringLiteral(".secondary"));
            const auto& layer = layers[i];
            pixels(prefix + QStringLiteral("x"), layer.x);
            pixels(prefix + QStringLiteral("y"), layer.y);
            pixels(prefix + QStringLiteral("blur"), layer.blur);
            pixels(prefix + QStringLiteral("spread"), layer.spread);
            add(prefix + QStringLiteral("opacity"), QString::number(layer.opacity));
        }
    }

    return tokens;
}

} // namespace choscordb::design
