#pragma once

#include <QColor>
#include <QList>
#include <QMetaType>
#include <QSize>
#include <QString>

namespace choscordb::design {

enum class DialogSize { Short, Export, Preferences, Profiles, Detail, Ddl };
enum class Spacing { Quarter, Half, One, OneHalf, Two, TwoHalf, Three, Four, Six, Eight };
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
    QuickSearchWidth,
    QuickSearchRow,
    CompletionPopupWidth,
    SheetWidth,
    TableRow,
    TableColumn,
    TableHeader,
    Row,
    GridHeader,
    Toolbar,
    NavigationRow,
    PaneTab,
    DocumentTab,
    Progress,
    ToastProgress,
    Badge,
    SwitchWidth,
    SwitchHeight,
    SwitchThumb
};
enum class Radius { Small, Medium, Large, ExtraLarge, TwoExtraLarge };
enum class Elevation { Popover, Dialog };
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

// Layout-only constants: window defaults and minimums, initial pane sizes and
// label limits. Sizes on the design scale come from Dimension, Spacing and Radius.
struct LayoutMetrics final {
    int modalHeaderHeight = 58;
    int modalFooterInset = 14;
    int modalFooterVerticalInset = 9;
    int connectionContentInset = 21;
    int connectionDriverHeight = 44;
    int connectionLabelCharacters = 16;
    int transactionLabelCharacters = 12;
    int navigationHighlightGap = 1;
    int narrowWorkspaceWidth = 1100;
    int defaultWorkspaceWidth = 1280;
    int defaultWorkspaceHeight = 900;
    int minimumWorkspaceWidth = 960;
    int minimumWorkspaceHeight = 640;
    int narrowNavigatorWidth = 235;
    int objectColumnRowHeight = 33;
    int sidebarInset = 11;
    int sidebarTopInset = 9;
    int initialEditorHeight = 380;
    int initialResultsHeight = 380;
    int initialHistoryHeight = 360;
    int minimumNavigatorWidth = 96;
    int minimumHistoryHeight = 80;
};

[[nodiscard]] constexpr LayoutMetrics layoutMetrics() {
    return {};
}
[[nodiscard]] QSize dialogInitialSize(DialogSize size);

} // namespace choscordb::design
