#include "design_system/metrics/metrics.h"

#include <utility>

namespace choscordb::design {
DesignMetrics resolveMetrics(Density, bool reducedMotion) {
    DesignMetrics metrics;
    metrics.controlHeight = dimension(Dimension::Button);
    metrics.dataRowHeight = dimension(Dimension::TableRow);
    metrics.navigationRowHeight = dimension(Dimension::NavigationRow);
    metrics.iconSmall = dimension(Dimension::Icon);
    metrics.iconLarge = dimension(Dimension::IconLarge);
    metrics.spacingSmall = spacing(Spacing::One);
    metrics.spacingMedium = spacing(Spacing::Two);
    metrics.spacingLarge = spacing(Spacing::Four);
    metrics.controlRadius = radius(Radius::Large);
    metrics.popoverRadius = radius(Radius::TwoExtraLarge);
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
    case DialogSize::Export:
        return {700, 400};
    case DialogSize::Preferences:
        return {700, 412};
    case DialogSize::Profiles:
        return {560, 440};
    case DialogSize::Detail:
        return {760, 480};
    case DialogSize::Ddl:
        return {700, 500};
    }
    return {560, 300};
}

int backdropBlurRadius() {
    return 3;
}

double iconStrokeWidth() {
    return 1.6;
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
        return 25;
    case Dimension::ButtonSmall:
        return 29;
    case Dimension::Button:
        return 33;
    case Dimension::ButtonLarge:
        return 37;
    case Dimension::Input:
        return 31;
    case Dimension::Selector:
        return 33;
    case Dimension::Checkbox:
        return 16;
    case Dimension::IconSmall:
        return 12;
    case Dimension::Icon:
        return 18;
    case Dimension::IconLarge:
        return 20;
    case Dimension::ModalWidth:
        return 700;
    case Dimension::TableRow:
        return 29;
    case Dimension::TableHeader:
        return 30;
    case Dimension::NavigationRow:
        return 33;
    case Dimension::PaneTab:
        return 35;
    case Dimension::DocumentTab:
        return 33;
    case Dimension::Progress:
        return 5;
    case Dimension::Badge:
        return 20;
    case Dimension::SwitchWidth:
        return 32;
    case Dimension::SwitchHeight:
        return 19;
    case Dimension::SwitchThumb:
        return 13;
    }
    return 0;
}

int radius(Radius value) {
    // Final compact MVP surfaces, in CSS pixels / Qt logical pixels.
    switch (value) {
    case Radius::Small:
        return 4;
    case Radius::Medium:
        return 5;
    case Radius::Large:
        return 7;
    case Radius::ExtraLarge:
        return 8;
    case Radius::TwoExtraLarge:
        return 10;
    case Radius::ThreeExtraLarge:
        return 12;
    case Radius::FourExtraLarge:
        return 14;
    }
    return 0;
}

QList<ShadowLayer> elevation(Elevation value) {
    switch (value) {
    case Elevation::None:
        return {};
    case Elevation::Medium:
        return {{0, 12, 40, 0, 0.15, QColor("#182c32")}};
    case Elevation::Large:
        return {{0, 16, 50, 0, 0.063, QColor("#243d35")}};
    case Elevation::Dialog:
        return {{0, 25, 90, 0, 0.2, QColor("#132b38")}};
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

} // namespace choscordb::design
