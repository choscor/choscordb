#include "design_system/field/field_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {
QString fieldSelectionStyleSheet() {
    return loadStyleSheet(QStringLiteral("field/field_selection_style_sheet.qss"));
}

QString fieldCaptionStyleSheet() {
    return loadStyleSheet(QStringLiteral("field/field_caption_style_sheet.qss"));
}

QString fieldBaseStyleSheet() {
    return loadStyleSheet(QStringLiteral("field/field_base_style_sheet.qss"));
}

QString fieldStateStyleSheet() {
    return loadStyleSheet(QStringLiteral("field/field_state_style_sheet.qss"));
}

QString fieldApplicationBaseStyleSheet() {
    return loadStyleSheet(QStringLiteral("field/field_application_base_style_sheet.qss"));
}

QString fieldApplicationFocusStyleSheet() {
    return loadStyleSheet(QStringLiteral("field/field_application_focus_style_sheet.qss"));
}
} // namespace choscordb::design
