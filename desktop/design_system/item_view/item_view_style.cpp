#include "design_system/item_view/item_view_style.h"

namespace choscordb::design {
QString itemViewStyleSheet() {
    return QStringLiteral(
        R"(QTableView, QTreeView, QListView { background: @field; alternate-background-color: @muted; color: @foreground; border: 0; gridline-color: @border; selection-background-color: @accent; selection-color: @foreground; outline: 0; }
)");
}

QString itemViewStateStyleSheet() {
    return QStringLiteral(
        R"(QTableView::item:selected, QTreeView::item:selected, QListView::item:selected { background: @accent; color: @foreground; }
QTableView::item:hover, QListView::item:hover { background: @accent; }
)");
}

QString itemViewApplicationStyleSheet() {
    return QStringLiteral(
        R"(QTreeView, QTableView { gridline-color: %3; selection-background-color: %12; }
)");
}
} // namespace choscordb::design
