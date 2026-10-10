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

QColor paletteColor(const QPalette& palette, QPalette::ColorRole role, const QColor& fallback) {
    const auto value = palette.color(QPalette::Active, role);
    return value.isValid() ? value : fallback;
}

} // namespace

double contrastRatio(const QColor& foreground, const QColor& background) {
    if (!foreground.isValid() || !background.isValid()) {
        return 0.0;
    }
    const auto lighter = std::max(luminance(foreground), luminance(background));
    const auto darker = std::min(luminance(foreground), luminance(background));
    return (lighter + 0.05) / (darker + 0.05);
}

Colors resolveColors(ResolvedAppearance appearance) {
    // Orange brand from the app mark on cool blue-slate neutrals; green is
    // reserved for success. Every text pair is at least 4.5:1 (WCAG 2).
    const bool dark = appearance == ResolvedAppearance::Dark;
    const QColor surface(dark ? "#20272b" : "#ffffff");
    const QColor fg(dark ? "#e0e8e8" : "#222b32");
    const QColor primary(dark ? "#ff8a18" : "#c2410c");
    return {
        .bg = QColor(dark ? "#171d20" : "#f6f7f8"),
        .surface = surface,
        .surfaceRaised = QColor(dark ? "#283135" : "#eef1f2"),
        .sidebar = QColor(dark ? "#1c2428" : "#fafbfb"),
        .fg = fg,
        .fgMuted = QColor(dark ? "#93a2a8" : "#5f6b72"),
        .fgDisabled = blend(fg, surface, 0.4),
        .border = QColor(dark ? "#343e43" : "#e3e7ea"),
        .primary = primary,
        .primaryHover = QColor(dark ? "#ff9b3d" : "#ad3a0b"),
        .primaryPressed = QColor(dark ? "#f07800" : "#9a330a"),
        .primaryFg = QColor(dark ? "#1f1206" : "#ffffff"),
        .selection = QColor(dark ? "#4a2c14" : "#fde4d3"),
        .ring = primary,
        .success = QColor(dark ? "#6cc08b" : "#2b7a4b"),
        .successSurface = QColor(dark ? "#213a2b" : "#e7f3eb"),
        .warning = QColor(dark ? "#e8c35a" : "#7d5a00"),
        .warningSurface = QColor(dark ? "#3d3418" : "#fff4d1"),
        .danger = QColor(dark ? "#f2878f" : "#b42336"),
        .dangerSurface = QColor(dark ? "#4a2228" : "#fceaec"),
        .backdrop = QColor(25, 44, 54, 80),
        .switchTrack = QColor(dark ? "#465359" : "#d8dfe0"),
        .codeKeyword = QColor(dark ? "#a984c8" : "#885da7"),
        .codeString = primary,
        .codeNumber = QColor(dark ? "#7fa8d6" : "#2f6aa3"),
        .codeComment = QColor(dark ? "#9ca6a7" : "#68737a"),
    };
}

Colors resolveForcedContrastColors(const QPalette& palette) {
    const QColor canvas = paletteColor(palette, QPalette::Window, QColor("#000000"));
    const QColor surface = paletteColor(palette, QPalette::Base, canvas);
    const QColor highlight = paletteColor(palette, QPalette::Highlight, QColor("#FFFFFF"));
    const QColor text = paletteColor(palette, QPalette::WindowText, QColor("#FFFFFF"));
    return {
        .bg = canvas,
        .surface = surface,
        .surfaceRaised = surface,
        .sidebar = surface,
        .fg = text,
        .fgMuted = text,
        .fgDisabled = paletteColor(palette, QPalette::PlaceholderText, text),
        .border = text,
        .primary = highlight,
        .primaryHover = highlight,
        .primaryPressed = highlight,
        .primaryFg = paletteColor(palette, QPalette::HighlightedText, canvas),
        .selection = surface,
        .ring = highlight,
        .success = text,
        .successSurface = surface,
        .warning = text,
        .warningSurface = surface,
        .danger = text,
        .dangerSurface = surface,
        .backdrop = QColor(0, 0, 0, 160),
        .switchTrack = surface,
        .codeKeyword = text,
        .codeString = text,
        .codeNumber = text,
        .codeComment = text,
    };
}

QColor switchThumb(const Colors& colors, ResolvedAppearance appearance) {
    return appearance == ResolvedAppearance::Dark ? QColor("#ffffff") : colors.surface;
}

QList<std::pair<QString, QColor>> colorTokens(const Colors& colors) {
    return {
        {QStringLiteral("bg"), colors.bg},
        {QStringLiteral("surface"), colors.surface},
        {QStringLiteral("surface-raised"), colors.surfaceRaised},
        {QStringLiteral("sidebar"), colors.sidebar},
        {QStringLiteral("fg"), colors.fg},
        {QStringLiteral("fg-muted"), colors.fgMuted},
        {QStringLiteral("fg-disabled"), colors.fgDisabled},
        {QStringLiteral("border"), colors.border},
        {QStringLiteral("primary"), colors.primary},
        {QStringLiteral("primary-hover"), colors.primaryHover},
        {QStringLiteral("primary-pressed"), colors.primaryPressed},
        {QStringLiteral("primary-fg"), colors.primaryFg},
        {QStringLiteral("selection"), colors.selection},
        {QStringLiteral("ring"), colors.ring},
        {QStringLiteral("success"), colors.success},
        {QStringLiteral("success-surface"), colors.successSurface},
        {QStringLiteral("warning"), colors.warning},
        {QStringLiteral("warning-surface"), colors.warningSurface},
        {QStringLiteral("danger"), colors.danger},
        {QStringLiteral("danger-surface"), colors.dangerSurface},
        {QStringLiteral("backdrop"), colors.backdrop},
        {QStringLiteral("switch-track"), colors.switchTrack},
        {QStringLiteral("code-keyword"), colors.codeKeyword},
        {QStringLiteral("code-string"), colors.codeString},
        {QStringLiteral("code-number"), colors.codeNumber},
        {QStringLiteral("code-comment"), colors.codeComment},
    };
}

} // namespace choscordb::design
