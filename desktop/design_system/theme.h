#pragma once

#include <QColor>
#include <QFont>
#include <QList>
#include <QMetaType>
#include <QPalette>
#include <QSize>
#include <QString>

class QWidget;

namespace choscordb::design {

enum class ThemeMode { System, Light, Dark };
enum class ResolvedAppearance { Light, Dark };
enum class Density { Compact, Comfortable };
enum class AccentPreset { Cobalt, Azure, Teal, Green, Violet, Orange, Rose };
enum class AccentKind { Preset, Custom };
enum class DialogSize { Short, Preferences, Profiles, Detail, Ddl };
enum class TypographyRole { Ui, Monospace, Small, Heading, Base, DialogTitle };

enum class Spacing { Half, One, OneHalf, Two, TwoHalf, Three, Four, Six, Eight };
enum class Dimension {
    ButtonExtraSmall,
    ButtonSmall,
    Button,
    ButtonLarge,
    Input,
    Checkbox,
    IconSmall,
    Icon,
    IconLarge,
    ModalWidth,
    TableRow
};
enum class Radius {
    Small,
    Medium,
    Large,
    ExtraLarge,
    TwoExtraLarge,
    ThreeExtraLarge,
    FourExtraLarge
};
enum class Elevation { None, Medium, Large };
enum class Motion { Interaction, Popup };

struct ShadowLayer final {
    int x = 0;
    int y = 0;
    int blur = 0;
    int spread = 0;
    double opacity = 0;
};

struct FocusSpec final {
    int borderWidth = 1;
    int ringWidth = 3;
    double referenceRingOpacity = 0.5;
};

struct MotionSpec final {
    int durationMs = 0;
    QString easing;
};

[[nodiscard]] int spacing(Spacing value);
[[nodiscard]] int dimension(Dimension value);
[[nodiscard]] int radius(Radius value);
[[nodiscard]] QList<ShadowLayer> elevation(Elevation value);
[[nodiscard]] FocusSpec focusSpec();
[[nodiscard]] MotionSpec motionSpec(Motion value, bool reducedMotion = false);

struct TypographySpec final {
    QString family;
    int pixelSize = 14;
    int lineHeight = 20;
    QFont::Weight weight = QFont::Normal;
};

struct DesignToken final {
    QString name;
    QString value;
    QString source;
};

[[nodiscard]] TypographySpec typographySpec(TypographyRole role);
[[nodiscard]] QList<DesignToken> designTokens(ResolvedAppearance appearance);
[[nodiscard]] bool bundledFontsAvailable();

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

    friend bool operator==(const SemanticColors&, const SemanticColors&) = default;
};

struct DesignMetrics final {
    int grid = 4;
    int spacingSmall = 4;
    int spacingMedium = 8;
    int spacingLarge = 16;
    int controlHeight = 32;
    int dataRowHeight = 37;
    int navigationRowHeight = 28;
    int workspaceChromeHeight = 36;
    int dialogContentSpacing = 16;
    int iconSmall = 16;
    int iconLarge = 20;
    int controlRadius = 10;
    int popoverRadius = 10;
    int dialogRadius = 14;
    int separatorWidth = 1;
    int dialogElevation = 0;
    int connectionLabelCharacters = 16;
    int transactionLabelCharacters = 12;
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
[[nodiscard]] ResolvedTheme resolvedThemeForWidget(const QWidget& widget);
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
