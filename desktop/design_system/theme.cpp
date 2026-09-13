#include "design_system/theme.h"
#include "design_system/control_style.h"
#include <QApplication>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <QDebug>
#include <QFontDatabase>

int qInitResources_resources();

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

SemanticColors resolveColors(ResolvedAppearance appearance, const Accent&) {
    const bool dark = appearance == ResolvedAppearance::Dark;
    const QColor canvas = dark ? QColor("#0a0a0a") : QColor("#ffffff");
    const QColor surface = dark ? QColor("#171717") : QColor("#ffffff");
    const QColor action = dark ? QColor("#e5e5e5") : QColor("#171717");
    const QColor actionText = dark ? QColor("#171717") : QColor("#fafafa");
    return {
        .background = canvas,
        .foreground = dark ? QColor("#fafafa") : QColor("#0a0a0a"),
        .card = surface,
        .cardForeground = dark ? QColor("#fafafa") : QColor("#0a0a0a"),
        .popover = surface,
        .popoverForeground = dark ? QColor("#fafafa") : QColor("#0a0a0a"),
        .primary = action,
        .primaryForeground = actionText,
        .secondary = dark ? QColor("#262626") : QColor("#f5f5f5"),
        .secondaryForeground = dark ? QColor("#fafafa") : QColor("#171717"),
        .muted = dark ? QColor("#262626") : QColor("#f5f5f5"),
        .mutedForeground = dark ? QColor("#a3a3a3") : QColor("#737373"),
        .accent = dark ? QColor("#262626") : QColor("#f5f5f5"),
        .accentForeground = dark ? QColor("#fafafa") : QColor("#171717"),
        .destructive = dark ? QColor("#ff6467") : QColor("#e7000b"),
        .border = dark ? QColor(255, 255, 255, 26) : QColor("#e5e5e5"),
        .input = dark ? QColor(255, 255, 255, 38) : QColor("#e5e5e5"),
        .ring = dark ? QColor("#737373") : QColor("#a3a3a3"),
        .sidebar = dark ? QColor("#171717") : QColor("#fafafa"),
        .sidebarForeground = dark ? QColor("#fafafa") : QColor("#0a0a0a"),
        .sidebarPrimary = dark ? QColor("#1447e6") : QColor("#171717"),
        .sidebarPrimaryForeground = QColor("#fafafa"),
        .sidebarAccent = dark ? QColor("#262626") : QColor("#f5f5f5"),
        .sidebarAccentForeground = dark ? QColor("#fafafa") : QColor("#171717"),
        .sidebarBorder = dark ? QColor(255, 255, 255, 26) : QColor("#e5e5e5"),
        .sidebarRing = dark ? QColor("#737373") : QColor("#a3a3a3"),
        .canvas = canvas,
        .surface = surface,
        .elevatedSurface = surface,
        .text = dark ? QColor("#fafafa") : QColor("#0a0a0a"),
        .mutedText = dark ? QColor("#a3a3a3") : QColor("#707070"),
        .action = action,
        .actionHover = blend(action, canvas, 0.8),
        .actionPressed = blend(action, canvas, 0.8),
        .actionText = actionText,
        .selection = action,
        .selectionText = actionText,
        // Accessibility exception: opaque 3:1 focus, versus upstream ring/50.
        .focus = dark ? QColor("#a3a3a3") : QColor("#737373"),
        .subtleAccent = dark ? QColor("#262626") : QColor("#f5f5f5"),
        .separator = dark ? blend(Qt::white, surface, 0.1) : QColor("#e5e5e5"),
        .disabled = blend(action, surface, 0.5),
        // Database status extensions; these are not upstream semantic roles.
        .success = dark ? QColor("#58b982") : QColor("#237a4b"),
        .warning = dark ? QColor("#e9b949") : QColor("#8a5700"),
        .danger = dark ? QColor("#ff6467") : QColor("#bf000a"),
        .neutral = dark ? QColor("#a3a3a3") : QColor("#737373"),
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
        .neutral = text,
    };
}

DesignMetrics resolveMetrics(Density, bool reducedMotion) {
    DesignMetrics metrics;
    metrics.controlHeight = dimension(Dimension::Button);
    metrics.dataRowHeight = dimension(Dimension::TableRow);
    metrics.iconSmall = dimension(Dimension::Icon);
    metrics.iconLarge = dimension(Dimension::IconLarge);
    metrics.spacingSmall = spacing(Spacing::One);
    metrics.spacingMedium = spacing(Spacing::Two);
    metrics.spacingLarge = spacing(Spacing::Four);
    metrics.controlRadius = radius(Radius::Large);
    metrics.popoverRadius = radius(Radius::Large);
    metrics.dialogRadius = radius(Radius::ExtraLarge);
    metrics.separatorWidth = focusSpec().borderWidth;
    metrics.animationDurationMs = motionSpec(Motion::Interaction, reducedMotion).durationMs;
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

bool bundledFontsAvailable() {
    static const bool loaded = [] {
        ::qInitResources_resources();
        bool available = true;
        for (const auto* weight : {"Regular", "Medium", "SemiBold", "Bold"}) {
            const auto path = QStringLiteral(":/fonts/Geist-%1.ttf").arg(QLatin1String(weight));
            if (QFontDatabase::addApplicationFont(path) < 0) {
                qWarning() << "Could not load bundled font:" << path;
                available = false;
            }
        }
        return available;
    }();
    return loaded;
}

TypographySpec typographySpec(TypographyRole role) {
    switch (role) {
    case TypographyRole::Small:
        return {QStringLiteral("Geist"), 12, 16, QFont::Normal};
    case TypographyRole::Heading:
        return {QStringLiteral("Geist"), 16, 22, QFont::Medium};
    case TypographyRole::Base:
        return {QStringLiteral("Geist"), 16, 24, QFont::Normal};
    case TypographyRole::DialogTitle:
        return {QStringLiteral("Geist"), 16, 16, QFont::Medium};
    case TypographyRole::Monospace:
        return {QFontDatabase::systemFont(QFontDatabase::FixedFont).family(), 14, 20,
                QFont::Normal};
    case TypographyRole::Ui:
        return {QStringLiteral("Geist"), 14, 20, QFont::Normal};
    }
    return {};
}

QFont resolveTypography(TypographyRole role) {
    if (role == TypographyRole::Monospace) {
        return QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    const auto spec = typographySpec(role);
    const auto fallback = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    QFont font = fallback;
    if (bundledFontsAvailable()) {
        font.setFamilies({spec.family, fallback.family()});
    }
    font.setPixelSize(spec.pixelSize);
    font.setWeight(spec.weight);
    return font;
}

int spacing(Spacing value) {
    switch (value) {
    case Spacing::Half:
        return 2;
    case Spacing::One:
        return 4;
    case Spacing::OneHalf:
        return 6;
    case Spacing::Two:
        return 8;
    case Spacing::TwoHalf:
        return 10;
    case Spacing::Three:
        return 12;
    case Spacing::Four:
        return 16;
    case Spacing::Six:
        return 24;
    case Spacing::Eight:
        return 32;
    }
    return 0;
}

int dimension(Dimension value) {
    switch (value) {
    case Dimension::ButtonExtraSmall:
        return 24;
    case Dimension::ButtonSmall:
        return 28;
    case Dimension::Button:
        return 32;
    case Dimension::ButtonLarge:
        return 36;
    case Dimension::Input:
        return 32;
    case Dimension::Checkbox:
        return 16;
    case Dimension::IconSmall:
        return 12;
    case Dimension::Icon:
        return 16;
    case Dimension::IconLarge:
        return 20;
    case Dimension::ModalWidth:
        return 384;
    case Dimension::TableRow:
        return 37;
    }
    return 0;
}

int radius(Radius value) {
    // Root 0.625rem, multiplicative scale in the pinned theming reference.
    switch (value) {
    case Radius::Small:
        return 6;
    case Radius::Medium:
        return 8;
    case Radius::Large:
        return 10;
    case Radius::ExtraLarge:
        return 14;
    case Radius::TwoExtraLarge:
        return 18;
    case Radius::ThreeExtraLarge:
        return 22;
    case Radius::FourExtraLarge:
        return 26;
    }
    return 0;
}

QList<ShadowLayer> elevation(Elevation value) {
    switch (value) {
    case Elevation::None:
        return {};
    case Elevation::Medium:
        return {{0, 4, 6, -1, 0.1}, {0, 2, 4, -2, 0.1}};
    case Elevation::Large:
        return {{0, 10, 15, -3, 0.1}, {0, 4, 6, -4, 0.1}};
    }
    return {};
}

FocusSpec focusSpec() {
    return {};
}

MotionSpec motionSpec(Motion value, bool reducedMotion) {
    return {reducedMotion ? 0 : (value == Motion::Popup ? 100 : 150),
            QStringLiteral("cubic-bezier(0.4, 0, 0.2, 1)")};
}

QList<DesignToken> designTokens(ResolvedAppearance appearance) {
    const auto colors = resolveColors(appearance, {});
    QList<DesignToken> tokens;
    const auto add = [&tokens](const QString& name, const QString& value) {
        tokens.append({name, value, QStringLiteral("desktop/design_system/theme.cpp")});
    };
    const auto color = [&add](const QString& name, const QColor& value) {
        add(QStringLiteral("color.") + name,
            value.alpha() == 255 ? value.name() : value.name(QColor::HexArgb));
    };
    color(QStringLiteral("background"), colors.background);
    color(QStringLiteral("foreground"), colors.foreground);
    color(QStringLiteral("card"), colors.card);
    color(QStringLiteral("card-foreground"), colors.cardForeground);
    color(QStringLiteral("popover"), colors.popover);
    color(QStringLiteral("popover-foreground"), colors.popoverForeground);
    color(QStringLiteral("primary"), colors.primary);
    color(QStringLiteral("primary-foreground"), colors.primaryForeground);
    color(QStringLiteral("secondary"), colors.secondary);
    color(QStringLiteral("secondary-foreground"), colors.secondaryForeground);
    color(QStringLiteral("muted"), colors.muted);
    color(QStringLiteral("muted-foreground"), colors.mutedForeground);
    color(QStringLiteral("accent"), colors.accent);
    color(QStringLiteral("accent-foreground"), colors.accentForeground);
    color(QStringLiteral("destructive"), colors.destructive);
    color(QStringLiteral("border"), colors.border);
    color(QStringLiteral("input"), colors.input);
    color(QStringLiteral("ring"), colors.ring);
    color(QStringLiteral("sidebar"), colors.sidebar);
    color(QStringLiteral("sidebar-foreground"), colors.sidebarForeground);
    color(QStringLiteral("sidebar-primary"), colors.sidebarPrimary);
    color(QStringLiteral("sidebar-primary-foreground"), colors.sidebarPrimaryForeground);
    color(QStringLiteral("sidebar-accent"), colors.sidebarAccent);
    color(QStringLiteral("sidebar-accent-foreground"), colors.sidebarAccentForeground);
    color(QStringLiteral("sidebar-border"), colors.sidebarBorder);
    color(QStringLiteral("sidebar-ring"), colors.sidebarRing);
    color(QStringLiteral("focus"), colors.focus);
    color(QStringLiteral("success"), colors.success);
    color(QStringLiteral("warning"), colors.warning);
    color(QStringLiteral("danger"), colors.danger);
    color(QStringLiteral("neutral"), colors.neutral);

    const auto pixels = [&add](const QString& name, int value) {
        add(name, QString::number(value) + QStringLiteral("px"));
    };
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
    pixels(QStringLiteral("radius.3xl"), radius(Radius::ThreeExtraLarge));
    pixels(QStringLiteral("radius.4xl"), radius(Radius::FourExtraLarge));
    pixels(QStringLiteral("size.button.xs"), dimension(Dimension::ButtonExtraSmall));
    pixels(QStringLiteral("size.button.sm"), dimension(Dimension::ButtonSmall));
    pixels(QStringLiteral("size.button.default"), dimension(Dimension::Button));
    pixels(QStringLiteral("size.button.lg"), dimension(Dimension::ButtonLarge));
    pixels(QStringLiteral("size.input"), dimension(Dimension::Input));
    pixels(QStringLiteral("size.checkbox"), dimension(Dimension::Checkbox));
    pixels(QStringLiteral("size.icon.small"), dimension(Dimension::IconSmall));
    pixels(QStringLiteral("size.icon.default"), dimension(Dimension::Icon));
    pixels(QStringLiteral("size.icon.large"), dimension(Dimension::IconLarge));
    pixels(QStringLiteral("size.modal.width"), dimension(Dimension::ModalWidth));
    pixels(QStringLiteral("size.table.row"), dimension(Dimension::TableRow));

    pixels(QStringLiteral("border.width"), focusSpec().borderWidth);
    pixels(QStringLiteral("focus.width"), focusSpec().ringWidth);
    add(QStringLiteral("focus.referenceOpacity"),
        QString::number(focusSpec().referenceRingOpacity));
    for (const auto& [name, role] :
         {std::pair{"ui", TypographyRole::Ui}, std::pair{"small", TypographyRole::Small},
          std::pair{"heading", TypographyRole::Heading}, std::pair{"base", TypographyRole::Base},
          std::pair{"dialogTitle", TypographyRole::DialogTitle},
          std::pair{"monospace", TypographyRole::Monospace}}) {
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
         {std::pair{"md", Elevation::Medium}, std::pair{"lg", Elevation::Large}}) {
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
    add(QStringLiteral("elevation.dialog"), QStringLiteral("none; foreground/10 border"));
    return tokens;
}

ResolvedTheme resolvedThemeForWidget(const QWidget& widget) {
    for (const QObject* scope = &widget; scope; scope = scope->parent()) {
        const auto theme = scope->property("designTheme");
        if (theme.canConvert<ResolvedTheme>())
            return theme.value<ResolvedTheme>();
    }
    if (qApp) {
        const auto theme = qApp->property("designTheme");
        if (theme.canConvert<ResolvedTheme>())
            return theme.value<ResolvedTheme>();
    }
    const auto appearance = widget.palette().color(QPalette::Window).lightnessF() < .5
                                ? ResolvedAppearance::Dark
                                : ResolvedAppearance::Light;
    return {appearance, resolveColors(appearance, {}), false};
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
    palette.setColor(QPalette::Link, colors.primary);
    palette.setColor(QPalette::Accent, colors.primary);
    palette.setColor(QPalette::Mid, colors.input);
    palette.setColor(QPalette::Dark, colors.focus);
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
    return style + controlStyleSheet(theme) +
           QStringLiteral(
               "QPushButton[designButton=\"true\"] { min-height: 0; padding: 0; border: 0; }");
}

} // namespace choscordb::design
