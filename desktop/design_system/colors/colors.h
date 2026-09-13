#pragma once

#include <QColor>
#include <QMetaType>
#include <QPalette>
#include <QString>

namespace choscordb::design {

enum class ThemeMode { System, Light, Dark };
enum class ResolvedAppearance { Light, Dark };
enum class AccentPreset { Cobalt, Azure, Teal, Green, Violet, Orange, Rose };
enum class AccentKind { Preset, Custom };
struct Accent final {
    AccentKind kind = AccentKind::Preset;
    AccentPreset preset = AccentPreset::Cobalt;
    QColor customColor;

    [[nodiscard]] static Accent presetColor(AccentPreset value);
    [[nodiscard]] static Accent custom(QColor value);
    [[nodiscard]] bool isCustom() const;

    friend bool operator==(const Accent&, const Accent&) = default;
};

struct AccentValidation final {
    bool accepted = false;
    QString reason;
};

struct SemanticColors final {
    QColor background;
    QColor foreground;
    QColor card;
    QColor cardForeground;
    QColor popover;
    QColor popoverForeground;
    QColor primary;
    QColor primaryForeground;
    QColor secondary;
    QColor secondaryForeground;
    QColor muted;
    QColor mutedForeground;
    QColor accent;
    QColor accentForeground;
    QColor destructive;
    QColor border;
    QColor input;
    QColor ring;
    QColor sidebar;
    QColor sidebarForeground;
    QColor sidebarPrimary;
    QColor sidebarPrimaryForeground;
    QColor sidebarAccent;
    QColor sidebarAccentForeground;
    QColor sidebarBorder;
    QColor sidebarRing;
    QColor canvas;
    QColor surface;
    QColor elevatedSurface;
    QColor text;
    QColor mutedText;
    QColor action;
    QColor actionHover;
    QColor actionPressed;
    QColor actionText;
    QColor selection;
    QColor selectionText;
    QColor focus;
    QColor subtleAccent;
    QColor separator;
    QColor disabled;
    QColor success;
    QColor warning;
    QColor danger;
    QColor neutral;
    QColor backdrop;
    QColor switchTrack;

    friend bool operator==(const SemanticColors&, const SemanticColors&) = default;
};

[[nodiscard]] double contrastRatio(const QColor& foreground, const QColor& background);
[[nodiscard]] QColor accentSeed(const Accent& accent);
[[nodiscard]] AccentValidation validateAccent(const Accent& accent, ResolvedAppearance appearance);
[[nodiscard]] SemanticColors resolveColors(ResolvedAppearance appearance, const Accent& accent);
[[nodiscard]] SemanticColors resolveForcedContrastColors(const QPalette& palette);

} // namespace choscordb::design

Q_DECLARE_METATYPE(choscordb::design::ThemeMode)
Q_DECLARE_METATYPE(choscordb::design::ResolvedAppearance)
Q_DECLARE_METATYPE(choscordb::design::AccentKind)
Q_DECLARE_METATYPE(choscordb::design::Accent)
