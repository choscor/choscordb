#pragma once

#include <QColor>
#include <QEasingCurve>
#include <QList>
#include <QMetaType>
#include <QSize>
#include <QString>

namespace choscordb::design {

enum class DialogSize { Short, Form, Preferences, Profiles, Detail };
enum class Spacing { Half, One, OneHalf, Two, TwoHalf, Three, Four, Six, Eight };
enum class Dimension {
    ControlExtraSmall,
    ControlSmall,
    Control,
    Checkbox,
    IconSmall,
    Icon,
    IconLarge,
    Logo,
    ModalWidth,
    QuickSearchWidth,
    CompletionPopupWidth,
    MenuWidth,
    SheetWidth,
    TableColumn,
    TableColumnMin,
    TableColumnMax,
    Row,
    TableRow,
    MenuRow,
    Header,
    Tab,
    DocumentTab,
    DocumentTabWidth,
    Toolbar,
    Progress,
    Scrollbar,
    ToastProgress,
    ToastWidth,
    Badge,
    SwitchWidth,
    SwitchHeight,
    SwitchThumb,
    SwitchRadius
};
enum class Radius { Small, Medium, Large, ExtraLarge };
enum class Elevation { Tooltip, Popover, Dialog };
enum class Motion { Interaction, Popup, LoaderTurn, LoaderDash };

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
    QEasingCurve easing;
};

[[nodiscard]] int spacing(Spacing value);
[[nodiscard]] int dimension(Dimension value);
[[nodiscard]] int radius(Radius value);
[[nodiscard]] QList<ShadowLayer> elevation(Elevation value);
[[nodiscard]] FocusSpec focusSpec();
[[nodiscard]] MotionSpec motionSpec(Motion value, bool reducedMotion = false);

struct LayoutMetrics final {
    int connectionContentInset = 21;
    int navigationHighlightGap = 1;
    int narrowWorkspaceWidth = 1100;
    int defaultWorkspaceWidth = 1280;
    int defaultWorkspaceHeight = 900;
    int minimumWorkspaceWidth = 960;
    int minimumWorkspaceHeight = 640;
    int narrowNavigatorWidth = 235;
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

}
