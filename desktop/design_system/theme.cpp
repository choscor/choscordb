#include "design_system/theme.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <QFontDatabase>

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

QColor shiftedLightness(const QColor& color, double difference) {
    QColor hsl = color.toHsl();
    hsl.setHslF(hsl.hslHueF(), hsl.hslSaturationF(),
                std::clamp(hsl.lightnessF() + difference, 0.0, 1.0));
    return hsl.toRgb();
}

QColor componentVariant(const QColor& action, const QColor& canvas, const QColor& surface,
                        const QColor& actionText, double desiredDifference) {
    for (int step = 10; step >= 0; --step) {
        const auto candidate = shiftedLightness(action, desiredDifference * step / 10.0);
        if (contrastRatio(candidate, canvas) >= 3.0 && contrastRatio(candidate, surface) >= 3.0 &&
            contrastRatio(actionText, candidate) >= 4.5) {
            return candidate;
        }
    }
    return action;
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
    return {.kind = AccentKind::Preset, .preset = value};
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

SemanticColors resolveColors(ResolvedAppearance appearance, const Accent& accent) {
    const bool dark = appearance == ResolvedAppearance::Dark;
    const QColor canvas = dark ? QColor("#111827") : QColor("#F5F7FA");
    const QColor surface = dark ? QColor("#182231") : QColor("#FFFFFF");
    const auto accessible = accessibleAccent(accentSeed(accent), canvas, surface);
    // Presets are reviewed inputs, so this fallback is unreachable for built-ins.
    const QColor action =
        accessible.found ? accessible.color : (dark ? QColor("#6CA6E3") : QColor("#2468B2"));
    const QColor actionText = accessible.found ? accessible.text : QColor("#FFFFFF");
    return {
        .canvas = canvas,
        .surface = surface,
        .elevatedSurface = dark ? QColor("#202D3D") : QColor("#FFFFFF"),
        .text = dark ? QColor("#F3F6FA") : QColor("#172033"),
        .mutedText = dark ? QColor("#B5C0CE") : QColor("#536174"),
        .action = action,
        .actionHover = componentVariant(action, canvas, surface, actionText, dark ? 0.06 : -0.05),
        .actionPressed =
            componentVariant(action, canvas, surface, actionText, dark ? -0.04 : -0.10),
        .actionText = actionText,
        .selection = action,
        .selectionText = actionText,
        .focus = action,
        .subtleAccent = blend(action, surface, dark ? 0.22 : 0.12),
        .separator = dark ? QColor("#344154") : QColor("#D8DEE8"),
        .disabled = dark ? QColor("#697688") : QColor("#9AA5B4"),
        .success = dark ? QColor("#58B982") : QColor("#237A4B"),
        .warning = dark ? QColor("#E9B949") : QColor("#8A5700"),
        .danger = dark ? QColor("#EF7B7B") : QColor("#B4232D"),
        .neutral = dark ? QColor("#94A3B8") : QColor("#64748B"),
    };
}

SemanticColors resolveForcedContrastColors(const QPalette& palette) {
    const QColor canvas = paletteColor(palette, QPalette::Window, QColor("#000000"));
    const QColor surface = paletteColor(palette, QPalette::Base, canvas);
    const QColor highlight = paletteColor(palette, QPalette::Highlight, QColor("#FFFFFF"));
    const QColor text = paletteColor(palette, QPalette::WindowText, QColor("#FFFFFF"));
    return {
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
        .neutral = text,
    };
}

DesignMetrics resolveMetrics(Density density, bool reducedMotion) {
    DesignMetrics metrics;
    if (density == Density::Comfortable) {
        metrics.spacingSmall = 8;
        metrics.spacingMedium = 12;
        metrics.spacingLarge = 20;
        metrics.controlHeight = 40;
        metrics.dataRowHeight = 36;
        metrics.workspaceChromeHeight = 44;
        metrics.dialogContentSpacing = 20;
        metrics.iconSmall = 20;
    }
    metrics.animationDurationMs = reducedMotion ? 0 : 150;
    metrics.animationsEnabled = !reducedMotion;
    return metrics;
}

QSize dialogInitialSize(DialogSize size) {
    switch (size) {
    case DialogSize::Short:
        return {560, 300};
    case DialogSize::Preferences:
        return {620, 520};
    case DialogSize::Profiles:
        return {780, 540};
    case DialogSize::Detail:
        return {760, 480};
    case DialogSize::Ddl:
        return {700, 500};
    }
    return {560, 300};
}

QFont resolveTypography(TypographyRole role) {
    return QFontDatabase::systemFont(
        role == TypographyRole::Monospace ? QFontDatabase::FixedFont : QFontDatabase::GeneralFont);
}

QPalette applicationPalette(const ResolvedTheme& theme) {
    const auto& colors = theme.colors;
    QPalette palette;
    palette.setColor(QPalette::Window, colors.canvas);
    palette.setColor(QPalette::WindowText, colors.text);
    palette.setColor(QPalette::Base, colors.surface);
    palette.setColor(QPalette::AlternateBase, colors.elevatedSurface);
    palette.setColor(QPalette::Text, colors.text);
    palette.setColor(QPalette::Button, colors.surface);
    palette.setColor(QPalette::ButtonText, colors.text);
    palette.setColor(QPalette::Highlight, colors.selection);
    palette.setColor(QPalette::HighlightedText, colors.selectionText);
    palette.setColor(QPalette::Link, colors.action);
    palette.setColor(QPalette::LinkVisited, colors.success);
    palette.setColor(QPalette::PlaceholderText, colors.mutedText);
    palette.setColor(QPalette::BrightText, colors.danger);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, colors.disabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, colors.disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, colors.disabled);
    return palette;
}

QString applicationStyleSheet(const ResolvedTheme& theme, const DesignMetrics& metrics) {
    const auto& colors = theme.colors;
    QString style = QStringLiteral(R"(
QPushButton, QToolButton, QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
  min-height: %1px;
  border: %2px solid %3;
  border-radius: %4px;
}
QPushButton, QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
  padding-left: %5px;
  padding-right: %5px;
}
QPushButton, QToolButton { background-color: %6; }
QPushButton:hover, QToolButton:hover { border-color: %7; }
QPushButton:default, QPushButton:checked, QToolButton:checked {
  color: %8;
  background-color: %9;
  border-color: %9;
}
QPushButton:default:hover, QPushButton:checked:hover, QToolButton:checked:hover {
  background-color: %10;
  border-color: %10;
}
QPushButton:default:pressed, QPushButton:checked:pressed, QToolButton:checked:pressed {
  background-color: %11;
  border-color: %11;
}
QPushButton:focus, QToolButton:focus, QLineEdit:focus, QComboBox:focus,
QSpinBox:focus, QDoubleSpinBox:focus, QTreeView:focus, QTableView:focus {
  border: %2px solid %7;
}
QTreeView, QTableView { gridline-color: %3; selection-background-color: %12; }
QTreeView::item { min-height: %13px; }
QHeaderView::section { min-height: %13px; border: 0; border-bottom: %2px solid %3; }
QToolBar { min-height: %14px; spacing: %15px; border: 0; border-bottom: %2px solid %3; }
QMenu { border: %2px solid %3; border-radius: %16px; padding: %15px; }
QToolTip { color: %17; background-color: %18; border: %2px solid %3; }
QTabBar::tab { min-height: %14px; padding-left: %5px; padding-right: %5px; }
QDockWidget::title { min-height: %14px; padding-left: %5px; }
QDialog[appDialog="true"] { border-radius: %22px; }
QDialog[appDialog="true"] QLabel[dialogDescription="true"] { color: %24; }
QDialog[appDialog="true"] QLabel[dialogStatus="true"] {
  border: %2px solid %3; border-radius: %4px; padding: %15px;
}
QPushButton[primary="true"], QToolButton[primary="true"] {
  color: %8; background-color: %9; border-color: %9;
}
QPushButton:pressed, QToolButton:pressed {
  background-color: %11; border-color: %11; color: %8;
}
QPushButton:disabled, QToolButton:disabled { color: %23; }
QLabel[state="completed"], QLabel[state="success"] { color: %19; }
QLabel[state="warning"] { color: %20; }
QLabel[state="failed"], QLabel[state="error"] { color: %21; }
QLabel#toastRegion { background-color: %18; border: %2px solid %3; padding: %5px; }
)");
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
                .arg(metrics.dataRowHeight)
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
    return style;
}

} // namespace choscordb::design
