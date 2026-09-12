#pragma once

#include <QColor>
#include <QFont>
#include <QMetaType>
#include <QPalette>
#include <QSize>
#include <QString>

namespace choscordb::design {

enum class ThemeMode { System, Light, Dark };
enum class ResolvedAppearance { Light, Dark };
enum class Density { Compact, Comfortable };
enum class AccentPreset { Cobalt, Azure, Teal, Green, Violet, Orange, Rose };
enum class AccentKind { Preset, Custom };
enum class DialogSize { Short, Preferences, Profiles, Detail, Ddl };
enum class TypographyRole { Ui, Monospace };

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

    friend bool operator==(const SemanticColors&, const SemanticColors&) = default;
};

struct DesignMetrics final {
    int grid = 4;
    int spacingSmall = 4;
    int spacingMedium = 8;
    int spacingLarge = 16;
    int controlHeight = 32;
    int dataRowHeight = 28;
    int workspaceChromeHeight = 36;
    int dialogContentSpacing = 16;
    int iconSmall = 16;
    int iconLarge = 20;
    int controlRadius = 6;
    int popoverRadius = 8;
    int dialogRadius = 10;
    int separatorWidth = 1;
    int dialogElevation = 16;
    int narrowWorkspaceWidth = 1100;
    int defaultWorkspaceWidth = 1280;
    int defaultWorkspaceHeight = 900;
    int minimumWorkspaceWidth = 960;
    int minimumWorkspaceHeight = 640;
    int initialNavigatorWidth = 245;
    int initialEditorHeight = 420;
    int initialResultsHeight = 340;
    int initialHistoryHeight = 360;
    int minimumNavigatorWidth = 96;
    int minimumHistoryHeight = 80;
    int animationDurationMs = 150;
    bool animationsEnabled = true;

    friend bool operator==(const DesignMetrics&, const DesignMetrics&) = default;
};

struct ResolvedTheme final {
    ResolvedAppearance appearance = ResolvedAppearance::Light;
    SemanticColors colors;
    bool forcedContrast = false;

    friend bool operator==(const ResolvedTheme&, const ResolvedTheme&) = default;
};

[[nodiscard]] double contrastRatio(const QColor& foreground, const QColor& background);
[[nodiscard]] QColor accentSeed(const Accent& accent);
[[nodiscard]] AccentValidation validateAccent(const Accent& accent, ResolvedAppearance appearance);
[[nodiscard]] SemanticColors resolveColors(ResolvedAppearance appearance, const Accent& accent);
[[nodiscard]] SemanticColors resolveForcedContrastColors(const QPalette& palette);
[[nodiscard]] DesignMetrics resolveMetrics(Density density, bool reducedMotion);
[[nodiscard]] QSize dialogInitialSize(DialogSize size);
[[nodiscard]] QFont resolveTypography(TypographyRole role);
[[nodiscard]] QPalette applicationPalette(const ResolvedTheme& theme);
[[nodiscard]] QString applicationStyleSheet(const ResolvedTheme& theme,
                                            const DesignMetrics& metrics);

} // namespace choscordb::design

Q_DECLARE_METATYPE(choscordb::design::ThemeMode)
Q_DECLARE_METATYPE(choscordb::design::ResolvedAppearance)
Q_DECLARE_METATYPE(choscordb::design::Density)
Q_DECLARE_METATYPE(choscordb::design::AccentKind)
Q_DECLARE_METATYPE(choscordb::design::Accent)
Q_DECLARE_METATYPE(choscordb::design::ResolvedTheme)
Q_DECLARE_METATYPE(choscordb::design::DesignMetrics)
