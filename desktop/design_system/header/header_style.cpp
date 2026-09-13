#include "design_system/header/header_style.h"

namespace choscordb::design {
QString headerStyleSheet() {
    return QStringLiteral(R"(QHeaderView { background: @background; color: @foreground; }
QHeaderView::section { min-height: @tableHeaderHeight; border: 0; border-bottom: 1px solid @border; padding: 0 12px; background: @muted; color: @mutedText; font-size: 11px; font-weight: 500; }
QTableCornerButton::section { border: 0; border-bottom: 1px solid @border; background: @background; }
)");
}

QString headerApplicationStyleSheet() {
    return QStringLiteral(
        R"(QHeaderView::section { min-height: %13px; border: 0; border-bottom: %2px solid %3; }
)");
}
} // namespace choscordb::design
