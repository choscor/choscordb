#include "design_system/metrics/metrics.h"

#include <utility>

namespace choscordb::design {
QSize dialogInitialSize(DialogSize size) {
    switch (size) {
    case DialogSize::Short:
        return {560, 300};
    case DialogSize::Export:
        return {700, 400};
    case DialogSize::Preferences:
        return {700, 412};
    case DialogSize::Profiles:
        return {860, 600};
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
        return 24;
    case Dimension::ControlSmall:
        return 28;
    case Dimension::Control:
        return 32;
    case Dimension::Checkbox:
        return 16;
    case Dimension::IconSmall:
        return 12;
    case Dimension::Icon:
        return 16;
    case Dimension::ModalWidth:
        return 700;
    case Dimension::QuickSearchWidth:
        return 640;
    case Dimension::QuickSearchRow:
        return 44;
    case Dimension::CompletionPopupWidth:
        return 420;
    case Dimension::SheetWidth:
        return 480;
    case Dimension::TableColumn:
        return 160;
    case Dimension::Row:
        return 28;
    case Dimension::Header:
        return 28;
    case Dimension::Tab:
        return 32;
    case Dimension::DocumentTabWidth:
        return 124;
    case Dimension::Toolbar:
        return 32;
    case Dimension::Progress:
        return 4;
    case Dimension::Scrollbar:
        return 10;
    case Dimension::ToastProgress:
        return 82;
    case Dimension::Badge:
        return 20;
    case Dimension::SwitchWidth:
        return 32;
    case Dimension::SwitchHeight:
        return 18;
    case Dimension::SwitchThumb:
        return 12;
    }
    return 0;
}

int radius(Radius value) {
    // Small: badges, kbd, menu items, progress. Medium: controls, rows,
    // toasts, tooltips. Large: menus, popovers, dialogs, sheets.
    switch (value) {
    case Radius::Small:
        return 4;
    case Radius::Medium:
        return 6;
    case Radius::Large:
        return 10;
    }
    return 0;
}

QList<ShadowLayer> elevation(Elevation value) {
    switch (value) {
    case Elevation::Popover:
        return {{0, 12, 40, 0, 0.15, QColor("#182c32")}};
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
