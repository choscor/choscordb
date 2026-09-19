#pragma once

#include <QColor>
#include <QList>
#include <QMetaType>
#include <QSize>
#include <QString>

namespace choscordb::design {

enum class Density { Compact, Comfortable };
enum class DialogSize { Short, Export, Preferences, Profiles, Detail, Ddl };
enum class Spacing { Half, One, OneHalf, Two, TwoHalf, Three, Four, Six, Eight };
enum class Dimension {
    ButtonExtraSmall,
    ButtonSmall,
    Button,
    ButtonLarge,
    Input,
    Selector,
    Checkbox,
    IconSmall,
    Icon,
    IconLarge,
    ModalWidth,
    TableRow,
    TableHeader,
    NavigationRow,
    PaneTab,
    DocumentTab,
    Progress,
    Badge,
    SwitchWidth,
    SwitchHeight,
    SwitchThumb
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
enum class Elevation { None, Medium, Large, Dialog };
enum class Motion { Interaction, Popup };

struct ShadowLayer final {
    int x = 0;
    int y = 0;
    int blur = 0;
    int spread = 0;
    double opacity = 0;
    QColor color = Qt::black;
};

struct FocusSpec final {
    int borderWidth = 1;
    int ringWidth = 2;
    double referenceRingOpacity = 1.0;
};

struct MotionSpec final {
    int durationMs = 0;
    QString easing;
};

[[nodiscard]] double iconStrokeWidth();
[[nodiscard]] int backdropBlurRadius();
[[nodiscard]] int spacing(Spacing value);
[[nodiscard]] int dimension(Dimension value);
[[nodiscard]] int radius(Radius value);
[[nodiscard]] QList<ShadowLayer> elevation(Elevation value);
[[nodiscard]] FocusSpec focusSpec();
[[nodiscard]] MotionSpec motionSpec(Motion value, bool reducedMotion = false);

struct DesignMetrics final {
    int grid = 4;
    int spacingSmall = 4;
    int spacingMedium = 8;
    int spacingLarge = 16;
    int controlHeight = 33;
    int dataRowHeight = 29;
    int sqlResultRowHeight = 35;
    int objectDataRowHeight = 35;
    int sqlResultHeaderHeight = 43;
    int navigationRowHeight = 33;
    int workspaceChromeHeight = 35;
    int dialogContentSpacing = 16;
    int modalHeaderHeight = 58;
    int modalContentInset = 16;
    int modalFooterInset = 14;
    int modalFooterVerticalInset = 9;
    int connectionContentInset = 21;
    int connectionHeaderHeight = 66;
    int connectionDriverHeight = 44;
    int iconSmall = 16;
    int iconLarge = 20;
    int controlRadius = 7;
    int popoverRadius = 10;
    int dialogRadius = 8;
    int separatorWidth = 1;
    int dialogElevation = 0;
    int connectionLabelCharacters = 16;
    int transactionLabelCharacters = 12;
    int narrowWorkspaceWidth = 1100;
    int defaultWorkspaceWidth = 1280;
    int defaultWorkspaceHeight = 900;
    int minimumWorkspaceWidth = 960;
    int minimumWorkspaceHeight = 640;
    int initialNavigatorWidth = 260;
    int narrowNavigatorWidth = 235;
    int objectColumnRowHeight = 33;
    int sidebarInset = 11;
    int sidebarTopInset = 9;
    int initialEditorResultsSplit = 430;
    int initialEditorHeight = 420;
    int initialResultsHeight = 340;
    int initialHistoryHeight = 360;
    int minimumNavigatorWidth = 96;
    int minimumHistoryHeight = 80;
    int animationDurationMs = 150;
    bool animationsEnabled = true;

    friend bool operator==(const DesignMetrics&, const DesignMetrics&) = default;
};

[[nodiscard]] DesignMetrics resolveMetrics(Density density, bool reducedMotion);
[[nodiscard]] QSize dialogInitialSize(DialogSize size);

} // namespace choscordb::design

Q_DECLARE_METATYPE(choscordb::design::Density)
Q_DECLARE_METATYPE(choscordb::design::DesignMetrics)
