#include "design_system/metrics/metrics.h"
#include <QPointF>

#include <utility>

namespace choscordb::design {
QSize dialogInitialSize(DialogSize size) {
    switch (size) {
    case DialogSize::Short:
        return {384, 300};
    case DialogSize::Form:
        return {700, 400};
    case DialogSize::Preferences:
        return {860, 452};
    case DialogSize::Profiles:
        return {860, 600};
    case DialogSize::Detail:
        return {760, 480};
    }
    return {384, 300};
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
    case Dimension::ControlExtraSmall:
        return 20;
    case Dimension::ControlSmall:
        return 26;
    case Dimension::Control:
        return 36;
    case Dimension::Checkbox:
        return 16;
    case Dimension::IconSmall:
        return 14;
    case Dimension::Icon:
        return 16;
    case Dimension::IconLarge:
        return 20;
    case Dimension::Logo:
        return 28;
    case Dimension::ModalWidth:
        return 700;
    case Dimension::QuickSearchWidth:
        return 672;
    case Dimension::CompletionPopupWidth:
        return 420;
    case Dimension::MenuWidth:
        return 144;
    case Dimension::SheetWidth:
        return 480;
    case Dimension::TableColumn:
        return 160;
    case Dimension::TableColumnMin:
        return 80;
    case Dimension::TableColumnMax:
        return 400;
    case Dimension::Row:
        return 34;
    case Dimension::TableRow:
        return 32;
    case Dimension::MenuRow:
        return 33;
    case Dimension::Header:
        return 36;
    case Dimension::Tab:
        return 36;
    case Dimension::DocumentTab:
        return 30;
    case Dimension::DocumentTabWidth:
        return 168;
    case Dimension::Toolbar:
        return 36;
    case Dimension::Progress:
        return 8;
    case Dimension::Scrollbar:
        return 8;
    case Dimension::ToastProgress:
        return 82;
    case Dimension::ToastWidth:
        return 340;
    case Dimension::Badge:
        return 20;
    case Dimension::SwitchWidth:
        return 36;
    case Dimension::SwitchHeight:
        return 18;
    case Dimension::SwitchThumb:
        return 18;
    case Dimension::SwitchRadius:
        return 5;
    }
    return 0;
}

int radius(Radius value) {
    switch (value) {
    case Radius::Small:
        return 4;
    case Radius::Medium:
        return 6;
    case Radius::Large:
        return 8;
    case Radius::ExtraLarge:
        return 12;
    }
    return 0;
}

QList<ShadowLayer> elevation(Elevation value) {
    switch (value) {
    case Elevation::Tooltip:
        return {{0, 4, 6, -1, 0.1}, {0, 2, 4, -2, 0.1}};
    case Elevation::Popover:
        return {{0, 10, 15, -3, 0.1}, {0, 4, 6, -4, 0.1}};
    case Elevation::Dialog:
        return {{0, 20, 25, -5, 0.03}, {0, 8, 10, -6, 0.03}};
    }
    return {};
}

FocusSpec focusSpec() {
    return {};
}

namespace {
QEasingCurve standardEasing() {
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(0.4, 0), QPointF(0.2, 1), QPointF(1, 1));
    return curve;
}
}
MotionSpec motionSpec(Motion value, bool reducedMotion) {
    if (reducedMotion)
        return {0, standardEasing()};
    switch (value) {
    case Motion::Popup:
        return {150, standardEasing()};
    case Motion::LoaderTurn:
        return {2000, QEasingCurve(QEasingCurve::Linear)};
    case Motion::LoaderDash:
        return {1500, QEasingCurve(QEasingCurve::Linear)};
    case Motion::Interaction:
        break;
    }
    return {100, standardEasing()};
}

}
