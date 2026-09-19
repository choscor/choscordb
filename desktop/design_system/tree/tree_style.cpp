#include "design_system/tree/tree_style.h"

namespace choscordb::design {
QString treeStyleSheet() {
    return QStringLiteral(R"(QTreeView { font-size: 12px; icon-size: 15px; }
)");
}

QString treeItemStyleSheet() {
    return QStringLiteral(
        R"(QTreeView::item { min-height: @navigationLineHeight; padding: 9px 10px; border-radius: 0; }
)");
}

QString treeStateStyleSheet() {
    return QStringLiteral(R"(QTreeView::item:hover { background: @muted; }
QTreeView::item:selected { background: @accent; color: @navigationText; font-weight: 600; }
)");
}

QString treeApplicationStyleSheet() {
    return QStringLiteral(R"(QTreeView::item { min-height: %13px; }
)");
}
} // namespace choscordb::design
