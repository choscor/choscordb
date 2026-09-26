#include "design_system/colors/colors.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

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

struct AccessibleAccent final {
    QColor color;
    QColor text;
    bool found = false;
};

AccessibleAccent accessibleAccent(const QColor& seed, const QColor& canvas, const QColor& surface) {
    constexpr double maxAdjustment = 0.22;
    const QColor lightText("#FFFFFF");
    const QColor darkText("#111827");
    AccessibleAccent best;
    double bestDistance = std::numeric_limits<double>::max();
    const QColor hsl = seed.toHsl();
    for (int step = -44; step <= 44; ++step) {
        const double difference = static_cast<double>(step) / 200.0;
        if (std::abs(difference) > maxAdjustment) {
            continue;
        }
        QColor candidate;
        candidate.setHslF(hsl.hslHueF(), hsl.hslSaturationF(),
                          std::clamp(hsl.lightnessF() + difference, 0.0, 1.0));
        candidate = candidate.toRgb();
        if (contrastRatio(candidate, canvas) < 3.0 || contrastRatio(candidate, surface) < 3.0) {
            continue;
        }
        QColor foreground;
        if (contrastRatio(lightText, candidate) >= 4.5) {
            foreground = lightText;
        } else if (contrastRatio(darkText, candidate) >= 4.5) {
            foreground = darkText;
        } else {
            continue;
        }
        if (std::abs(difference) < bestDistance) {
            best = {candidate, foreground, true};
            bestDistance = std::abs(difference);
        }
    }
    return best;
}

QColor paletteColor(const QPalette& palette, QPalette::ColorRole role, const QColor& fallback) {
    const auto value = palette.color(QPalette::Active, role);
    return value.isValid() ? value : fallback;
}

} // namespace

Accent Accent::presetColor(AccentPreset value) {
    return {.kind = AccentKind::Preset, .preset = value, .customColor = {}};
}

Accent Accent::custom(QColor value) {
    return {.kind = AccentKind::Custom,
            .preset = AccentPreset::Cobalt,
            .customColor = std::move(value)};
}

bool Accent::isCustom() const {
    return kind == AccentKind::Custom;
}

double contrastRatio(const QColor& foreground, const QColor& background) {
    if (!foreground.isValid() || !background.isValid()) {
        return 0.0;
    }
    const auto lighter = std::max(luminance(foreground), luminance(background));
    const auto darker = std::min(luminance(foreground), luminance(background));
    return (lighter + 0.05) / (darker + 0.05);
}

QColor accentSeed(const Accent& accent) {
    if (accent.isCustom()) {
        return accent.customColor;
    }
    switch (accent.preset) {
    case AccentPreset::Cobalt:
        return QColor("#2F7DD3");
    case AccentPreset::Azure:
        return QColor("#0077B6");
    case AccentPreset::Teal:
        return QColor("#087F8C");
    case AccentPreset::Green:
        return QColor("#2E7D32");
    case AccentPreset::Violet:
        return QColor("#7456C8");
    case AccentPreset::Orange:
        return QColor("#B85C00");
    case AccentPreset::Rose:
        return QColor("#B74668");
    }
    return QColor("#2F7DD3");
}

AccentValidation validateAccent(const Accent& accent, ResolvedAppearance appearance) {
    const QColor seed = accentSeed(accent);
    if (!seed.isValid()) {
        return {false, QStringLiteral("Choose a valid RGB color.")};
    }
    if (seed.alpha() != 255) {
        return {false, QStringLiteral("Accent colors must be fully opaque.")};
    }
    const QColor canvas =
        appearance == ResolvedAppearance::Dark ? QColor("#111827") : QColor("#F5F7FA");
    const QColor surface =
        appearance == ResolvedAppearance::Dark ? QColor("#182231") : QColor("#FFFFFF");
    if (!accessibleAccent(seed, canvas, surface).found) {
        return {false,
                QStringLiteral("This color cannot provide readable actions and focus indicators.")};
    }
    return {true, {}};
}

SemanticColors resolveColors(ResolvedAppearance appearance, const Accent&) {
    // Final MVP :root/.dark values. Legacy accent preferences remain readable,
    // but no longer alter the replacement appearance.
    const bool dark = appearance == ResolvedAppearance::Dark;
    const QColor canvas(dark ? "#171d20" : "#f6f7f8");
    const QColor panel(dark ? "#20272b" : "#ffffff");
    const QColor text(dark ? "#e0e8e8" : "#222b32");
    const QColor soft(dark ? "#303030" : "#f2f2f2");
    const QColor green(dark ? "#65b493" : "#287f66");
    const QColor greenText(dark ? "#12231b" : "#ffffff");
    const QColor greenBackground(dark ? "#254b38" : "#ccebdc");
    const QColor line(dark ? "#343e43" : "#e7ebed");
    const QColor sidebar(dark ? "#1c2428" : "#fafbfb");
    // Accessibility extensions: the reference's light muted/red text does not
    // reach 4.5:1. Keep the hue and increase contrast on panel/soft surfaces.
    const QColor mutedText(dark ? "#8e9da3" : "#626e75");
    const QColor danger(dark ? "#e58c85" : "#a5413d");
    return {
        .background = canvas,
        .foreground = text,
        .card = panel,
        .cardForeground = text,
        .popover = panel,
        .popoverForeground = text,
        .primary = green,
        .primaryForeground = greenText,
        .secondary = soft,
        .secondaryForeground = text,
        .muted = soft,
        .mutedForeground = mutedText,
        .accent = greenBackground,
        .accentForeground = text,
        .destructive = QColor("#c45d58"),
        .border = line,
        .input = line,
        .ring = green,
        .sidebar = sidebar,
        .sidebarForeground = text,
        .sidebarPrimary = green,
        .sidebarPrimaryForeground = greenText,
        .sidebarAccent = greenBackground,
        .sidebarAccentForeground = dark ? green : QColor("#24765e"),
        .sidebarBorder = line,
        .sidebarRing = green,
        .canvas = canvas,
        .surface = panel,
        .elevatedSurface = soft,
        .text = text,
        .mutedText = mutedText,
        .action = green,
        .actionHover = green,
        .actionPressed = green,
        .actionText = greenText,
        .selection = greenBackground,
        .selectionText = text,
        .focus = green,
        .subtleAccent = greenBackground,
        .separator = line,
        .disabled = blend(text, panel, 0.4),
        .success = green,
        .warning = QColor(dark ? "#e0bb72" : "#805d24"),
        .danger = danger,
        .successSurface = QColor(dark ? "#283e34" : "#eaf4ef"),
        .warningSurface = QColor(dark ? "#473b22" : "#fff3d6"),
        .dangerSurface = QColor(dark ? "#492d2b" : "#fdecea"),
        .neutral = mutedText,
        .backdrop = QColor(25, 44, 54, 80),
        .switchTrack = QColor(dark ? "#465359" : "#d8dfe0"),
        // Preserve prototype syntax hues; adjust only colors below 4.5:1 on
        // the editor panel (light comments/numbers and dark keywords).
        .sqlKeyword = QColor(dark ? "#a984c8" : "#885da7"),
        .sqlString = green,
        .sqlComment = QColor(dark ? "#9ca6a7" : "#6f7879"),
        .sqlNumber = QColor(dark ? "#b3834f" : "#936b3f"),
        .sqliteBadgeBackground = QColor("#f6f0e6"),
        .sqliteBadgeForeground = QColor("#ad8a51"),
        .sqliteBadgeBorder = QColor("#eae1d3"),
        .postgresBadgeBackground = QColor("#edf3f9"),
        .postgresBadgeForeground = QColor("#6288ab"),
        .postgresBadgeBorder = QColor("#dce7ef"),
    };
}

SemanticColors resolveForcedContrastColors(const QPalette& palette) {
    const QColor canvas = paletteColor(palette, QPalette::Window, QColor("#000000"));
    const QColor surface = paletteColor(palette, QPalette::Base, canvas);
    const QColor highlight = paletteColor(palette, QPalette::Highlight, QColor("#FFFFFF"));
    const QColor text = paletteColor(palette, QPalette::WindowText, QColor("#FFFFFF"));
    return {
        .background = canvas,
        .foreground = text,
        .card = surface,
        .cardForeground = text,
        .popover = surface,
        .popoverForeground = text,
        .primary = highlight,
        .primaryForeground = paletteColor(palette, QPalette::HighlightedText, canvas),
        .secondary = surface,
        .secondaryForeground = text,
        .muted = surface,
        .mutedForeground = text,
        .accent = surface,
        .accentForeground = text,
        .destructive = text,
        .border = text,
        .input = text,
        .ring = highlight,
        .sidebar = surface,
        .sidebarForeground = text,
        .sidebarPrimary = highlight,
        .sidebarPrimaryForeground = paletteColor(palette, QPalette::HighlightedText, canvas),
        .sidebarAccent = surface,
        .sidebarAccentForeground = text,
        .sidebarBorder = text,
        .sidebarRing = highlight,
        .canvas = canvas,
        .surface = surface,
        .elevatedSurface = surface,
        .text = text,
        .mutedText = text,
        .action = highlight,
        .actionHover = highlight,
        .actionPressed = highlight,
        .actionText = paletteColor(palette, QPalette::HighlightedText, canvas),
        .selection = highlight,
        .selectionText = paletteColor(palette, QPalette::HighlightedText, canvas),
        .focus = highlight,
        .subtleAccent = surface,
        .separator = text,
        .disabled = paletteColor(palette, QPalette::PlaceholderText, text),
        .success = text,
        .warning = text,
        .danger = text,
        .successSurface = surface,
        .warningSurface = surface,
        .dangerSurface = surface,
        .neutral = text,
        .backdrop = QColor(0, 0, 0, 160),
        .switchTrack = surface,
        .sqlKeyword = text,
        .sqlString = text,
        .sqlComment = text,
        .sqlNumber = text,
        .sqliteBadgeBackground = surface,
        .sqliteBadgeForeground = text,
        .sqliteBadgeBorder = text,
        .postgresBadgeBackground = surface,
        .postgresBadgeForeground = text,
        .postgresBadgeBorder = text,
    };
}

} // namespace choscordb::design
