#include "design_system/colors/colors.h"

#include <algorithm>
#include <cmath>

namespace choscordb::design {
namespace {

double linearChannel(double channel) {
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double luminance(const QColor& color) {
    return 0.2126 * linearChannel(color.redF()) + 0.7152 * linearChannel(color.greenF()) +
           0.0722 * linearChannel(color.blueF());
}

QColor blend(const QColor& foreground, const QColor& background, double amount) {
    const auto channel = [amount](int front, int back) {
        return qRound(front * amount + back * (1.0 - amount));
    };
    return {channel(foreground.red(), background.red()),
            channel(foreground.green(), background.green()),
            channel(foreground.blue(), background.blue())};
}

QColor withAlpha(QColor color, double alpha) {
    color.setAlphaF(alpha);
    return color;
}

QColor cssHex(QStringView hex) {
    const auto channel = [hex](qsizetype index) { return hex.sliced(index, 2).toInt(nullptr, 16); };
    return {channel(1), channel(3), channel(5), hex.size() == 9 ? channel(7) : 255};
}

QColor paletteColor(const QPalette& palette, QPalette::ColorRole role, const QColor& fallback) {
    const auto value = palette.color(QPalette::Active, role);
    return value.isValid() ? value : fallback;
}

}

double contrastRatio(const QColor& foreground, const QColor& background) {
    if (!foreground.isValid() || !background.isValid()) {
        return 0.0;
    }
    const auto lighter = std::max(luminance(foreground), luminance(background));
    const auto darker = std::min(luminance(foreground), luminance(background));
    return (lighter + 0.05) / (darker + 0.05);
}

QColor composite(const QColor& top, const QColor& bottom) {
    return blend(top, bottom, top.alphaF());
}

Colors resolveColors(ResolvedAppearance appearance) {
    const bool dark = appearance == ResolvedAppearance::Dark;
    const auto pick = [dark](QStringView light, QStringView darkValue) {
        return cssHex(dark ? darkValue : light);
    };
    return {
        .canvas = pick(u"#fbfbfb", u"#030303"),
        .elevated = pick(u"#f8f8f8", u"#060606"),
        .recessed = pick(u"#f3f3f3", u"#0b0b0b"),
        .overlay = pick(u"#fbfbfb", u"#262626"),
        .base = pick(u"#ffffff", u"#0f0f0f"),
        .control = pick(u"#ffffff", u"#171717"),
        .tint = pick(u"#f5f5f5", u"#262626"),
        .fill = pick(u"#e5e5e5", u"#262626"),
        .contrast = pick(u"#060606", u"#fafafa"),
        .badgeInverted = pick(u"#0a0a0a", u"#ffffff"),
        .brand = pick(u"#056dff", u"#045ede"),
        .brandHover = pick(u"#1447e6", u"#1447e6"),
        .line = pick(u"#0a0a0a1a", u"#333333"),
        .hairline = pick(u"#e9e9e9", u"#262626"),
        .focus = pick(u"#0b0b0b", u"#e9e9e9"),
        .shadowEdge = pick(u"#0000001f", u"#ffffff1a"),
        .shadowDrop = pick(u"#00000014", u"#0000004c"),
        .arrowEdge = pick(u"#0a0a0a1a", u"#00000000"),
        .arrowStroke = pick(u"#00000000", u"#333333"),
        .danger = pick(u"#fb2c36", u"#e7000b"),
        .successTint = pick(u"#dcfce791", u"#0c542b33"),
        .warningTint = pick(u"#ffe89433", u"#5731005e"),
        .dangerTint = pick(u"#ffe2e26b", u"#9900032b"),
        .textDefault = pick(u"#171717", u"#f5f5f5"),
        .textStrong = pick(u"#0a0a0a", u"#fafafa"),
        .textInverse = pick(u"#f5f5f5", u"#171717"),
        .textSubtle = pick(u"#737373", u"#a1a1a1"),
        .textInactive = pick(u"#d4d4d4", u"#525252"),
        .textPlaceholder = pick(u"#a1a1a1", u"#737373"),
        .textSuccess = pick(u"#006045", u"#a4f4cf"),
        .textDanger = pick(u"#c10007", u"#ff6467"),
        .textWarning = pick(u"#bd6500", u"#ff8904"),
        .textBadgeInverted = pick(u"#ffffff", u"#000000"),
        .textBadgeNeutralSubtle = pick(u"#262626", u"#e5e5e5"),
        .white = pick(u"#ffffff", u"#ffffff"),
        .codeKeyword = pick(u"#d73a49", u"#a0a0a0"),
        .codeString = pick(u"#032f62", u"#99ffe4"),
        .codeNumber = pick(u"#005cc5", u"#ffc799"),
        .codeComment = pick(u"#6a737d", u"#8b8b8b94"),
        .codeKey = pick(u"#005cc5", u"#ffc799"),
    };
}

Colors resolveForcedContrastColors(const QPalette& palette) {
    const QColor canvas = paletteColor(palette, QPalette::Window, QColor("#000000"));
    const QColor surface = paletteColor(palette, QPalette::Base, canvas);
    const QColor highlight = paletteColor(palette, QPalette::Highlight, QColor("#FFFFFF"));
    const QColor text = paletteColor(palette, QPalette::WindowText, QColor("#FFFFFF"));
    return {
        .canvas = canvas,
        .elevated = surface,
        .recessed = canvas,
        .overlay = withAlpha(text, 0.2),
        .base = surface,
        .control = surface,
        .tint = withAlpha(text, 0.2),
        .fill = withAlpha(text, 0.35),
        .contrast = highlight,
        .badgeInverted = text,
        .brand = highlight,
        .brandHover = highlight,
        .line = text,
        .hairline = text,
        .focus = text,
        .shadowEdge = QColor(Qt::transparent),
        .shadowDrop = QColor(Qt::transparent),
        .arrowEdge = text,
        .arrowStroke = QColor(Qt::transparent),
        .danger = text,
        .successTint = surface,
        .warningTint = surface,
        .dangerTint = surface,
        .textDefault = text,
        .textStrong = text,
        .textInverse = paletteColor(palette, QPalette::HighlightedText, canvas),
        .textSubtle = text,
        .textInactive = paletteColor(palette, QPalette::PlaceholderText, text),
        .textPlaceholder = paletteColor(palette, QPalette::PlaceholderText, text),
        .textSuccess = text,
        .textDanger = text,
        .textWarning = text,
        .textBadgeInverted = paletteColor(palette, QPalette::HighlightedText, canvas),
        .textBadgeNeutralSubtle = text,
        .white = paletteColor(palette, QPalette::HighlightedText, canvas),
        .codeKeyword = text,
        .codeString = text,
        .codeNumber = text,
        .codeComment = text,
        .codeKey = text,
    };
}

QColor withOpacity(const QColor& token, double opacity) {
    return withAlpha(token, token.alphaF() * opacity);
}

QColor dialogBackdrop(const Colors& colors, Scrim scrim) {
    return withOpacity(scrim == Scrim::CommandPalette ? colors.overlay : colors.recessed, 0.8);
}

BannerColors secondaryBanner(const Colors& colors) {
    const auto fill = composite(withOpacity(colors.contrast, 0.05), colors.base);
    return {fill, composite(withOpacity(colors.textDefault, 0.7), fill)};
}

SwitchColors switchColors(const Colors& colors, ResolvedAppearance appearance, bool forcedContrast,
                          bool checked) {
    if (forcedContrast)
        return checked ? SwitchColors{colors.brand, colors.line, colors.white}
                       : SwitchColors{colors.base, colors.line, colors.textDefault};
    const bool dark = appearance == ResolvedAppearance::Dark;
    const auto pick = [dark](QStringView light, QStringView darkValue) {
        return cssHex(dark ? darkValue : light);
    };
    if (checked)
        return {pick(u"#2b7fff", u"#155dfc"), pick(u"#155dfc", u"#2b7fff"),
                dark ? cssHex(u"#8ec5ff") : colors.base};
    return {pick(u"#e5e5e5", u"#404040"), pick(u"#d4d4d4", u"#525252"),
            dark ? cssHex(u"#1f1f1f") : colors.base};
}

EmphasisFill emphasisFill(const Colors& colors, ResolvedAppearance appearance, bool forcedContrast,
                          Emphasis emphasis) {
    const bool danger = emphasis == Emphasis::Danger;
    const QColor token = danger ? colors.danger : colors.brand;
    if (forcedContrast)
        return {token, token, token, token};
    const bool dark = appearance == ResolvedAppearance::Dark;
    const auto pick = [dark](QStringView light, QStringView darkValue) {
        return cssHex(dark ? darkValue : light);
    };
    if (danger)
        return {pick(u"#da252e", u"#c90008"), pick(u"#ff7d75", u"#f86e62"),
                pick(u"#ff5b57", u"#f0463e"), token};
    return {pick(u"#045ede", u"#0351c1"), pick(u"#619eff", u"#5c92ec"),
            pick(u"#3c86ff", u"#3879e5"), token};
}

QList<std::pair<QString, QColor>> colorTokens(const Colors& colors) {
    return {
        {QStringLiteral("kumo-canvas"), colors.canvas},
        {QStringLiteral("kumo-elevated"), colors.elevated},
        {QStringLiteral("kumo-recessed"), colors.recessed},
        {QStringLiteral("kumo-overlay"), colors.overlay},
        {QStringLiteral("kumo-base"), colors.base},
        {QStringLiteral("kumo-control"), colors.control},
        {QStringLiteral("kumo-tint"), colors.tint},
        {QStringLiteral("kumo-fill"), colors.fill},
        {QStringLiteral("kumo-contrast"), colors.contrast},
        {QStringLiteral("kumo-badge-inverted"), colors.badgeInverted},
        {QStringLiteral("kumo-brand"), colors.brand},
        {QStringLiteral("kumo-brand-hover"), colors.brandHover},
        {QStringLiteral("kumo-line"), colors.line},
        {QStringLiteral("kumo-hairline"), colors.hairline},
        {QStringLiteral("kumo-focus"), colors.focus},
        {QStringLiteral("kumo-shadow-edge"), colors.shadowEdge},
        {QStringLiteral("kumo-shadow-drop"), colors.shadowDrop},
        {QStringLiteral("kumo-arrow-edge"), colors.arrowEdge},
        {QStringLiteral("kumo-arrow-stroke"), colors.arrowStroke},
        {QStringLiteral("kumo-danger"), colors.danger},
        {QStringLiteral("kumo-success-tint"), colors.successTint},
        {QStringLiteral("kumo-warning-tint"), colors.warningTint},
        {QStringLiteral("kumo-danger-tint"), colors.dangerTint},
        {QStringLiteral("text-kumo-default"), colors.textDefault},
        {QStringLiteral("text-kumo-strong"), colors.textStrong},
        {QStringLiteral("text-kumo-inverse"), colors.textInverse},
        {QStringLiteral("text-kumo-subtle"), colors.textSubtle},
        {QStringLiteral("text-kumo-inactive"), colors.textInactive},
        {QStringLiteral("text-kumo-placeholder"), colors.textPlaceholder},
        {QStringLiteral("text-kumo-success"), colors.textSuccess},
        {QStringLiteral("text-kumo-danger"), colors.textDanger},
        {QStringLiteral("text-kumo-warning"), colors.textWarning},
        {QStringLiteral("text-kumo-badge-inverted"), colors.textBadgeInverted},
        {QStringLiteral("text-kumo-badge-neutral-subtle"), colors.textBadgeNeutralSubtle},
        {QStringLiteral("white"), colors.white},
        {QStringLiteral("code-keyword"), colors.codeKeyword},
        {QStringLiteral("code-string"), colors.codeString},
        {QStringLiteral("code-number"), colors.codeNumber},
        {QStringLiteral("code-comment"), colors.codeComment},
        {QStringLiteral("code-key"), colors.codeKey},
    };
}

}
