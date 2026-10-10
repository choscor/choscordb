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
    pixels(QStringLiteral("spacing.0.5"), spacing(Spacing::Half));
    pixels(QStringLiteral("spacing.1"), spacing(Spacing::One));
    pixels(QStringLiteral("spacing.1.5"), spacing(Spacing::OneHalf));
    pixels(QStringLiteral("spacing.2"), spacing(Spacing::Two));
    pixels(QStringLiteral("spacing.3"), spacing(Spacing::Three));
    pixels(QStringLiteral("spacing.4"), spacing(Spacing::Four));
    pixels(QStringLiteral("spacing.6"), spacing(Spacing::Six));
    pixels(QStringLiteral("spacing.8"), spacing(Spacing::Eight));
    pixels(QStringLiteral("radius.sm"), radius(Radius::Small));
    pixels(QStringLiteral("radius.md"), radius(Radius::Medium));
    pixels(QStringLiteral("radius.lg"), radius(Radius::Large));
    pixels(QStringLiteral("size.control.xs"), dimension(Dimension::ControlExtraSmall));
    pixels(QStringLiteral("size.control.sm"), dimension(Dimension::ControlSmall));
    pixels(QStringLiteral("size.control"), dimension(Dimension::Control));
    pixels(QStringLiteral("size.row"), dimension(Dimension::Row));
    pixels(QStringLiteral("size.header"), dimension(Dimension::Header));
    pixels(QStringLiteral("size.tab"), dimension(Dimension::Tab));
    pixels(QStringLiteral("size.tab.document.width"), dimension(Dimension::DocumentTabWidth));
    pixels(QStringLiteral("size.toolbar"), dimension(Dimension::Toolbar));
    pixels(QStringLiteral("size.quickSearch.row"), dimension(Dimension::QuickSearchRow));
    pixels(QStringLiteral("size.icon.sm"), dimension(Dimension::IconSmall));
    pixels(QStringLiteral("size.icon"), dimension(Dimension::Icon));
    pixels(QStringLiteral("size.checkbox"), dimension(Dimension::Checkbox));
    pixels(QStringLiteral("size.switch.width"), dimension(Dimension::SwitchWidth));
    pixels(QStringLiteral("size.switch.height"), dimension(Dimension::SwitchHeight));
    pixels(QStringLiteral("size.switch.thumb"), dimension(Dimension::SwitchThumb));
    pixels(QStringLiteral("size.progress"), dimension(Dimension::Progress));
    pixels(QStringLiteral("size.scrollbar"), dimension(Dimension::Scrollbar));
    pixels(QStringLiteral("size.badge"), dimension(Dimension::Badge));
    pixels(QStringLiteral("size.toast.progress"), dimension(Dimension::ToastProgress));
    pixels(QStringLiteral("size.modal.width"), dimension(Dimension::ModalWidth));
    pixels(QStringLiteral("size.quickSearch.width"), dimension(Dimension::QuickSearchWidth));
    pixels(QStringLiteral("size.sheet.width"), dimension(Dimension::SheetWidth));
    pixels(QStringLiteral("size.completion.width"), dimension(Dimension::CompletionPopupWidth));
    pixels(QStringLiteral("size.table.column"), dimension(Dimension::TableColumn));
    add(QStringLiteral("icon.stroke"), QString::number(iconStrokeWidth()) + "px");

    pixels(QStringLiteral("border.width"), focusSpec().borderWidth);
    pixels(QStringLiteral("focus.width"), focusSpec().ringWidth);
    add(QStringLiteral("focus.referenceOpacity"),
        QString::number(focusSpec().referenceRingOpacity));
    for (const auto& [name, role] :
         {std::pair{"caption", TypographyRole::Caption}, std::pair{"small", TypographyRole::Small},
          std::pair{"dense", TypographyRole::Dense}, std::pair{"body", TypographyRole::Body},
          std::pair{"title", TypographyRole::Title}, std::pair{"mono", TypographyRole::Mono},
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
