#include "design_system/tree/navigation_tree_view.h"
#include "design_system/theme.h"
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QPainter>

namespace choscordb::design {

NavigationTreeView::NavigationTreeView(QWidget* parent) : QTreeView(parent) {
    setProperty("designSurface", "sidebar");
    setProperty("designNavigationTree", true);
    setMouseTracking(true);
}

void NavigationTreeView::mouseMoveEvent(QMouseEvent* event) {
    hoveredPosition_ = event->position().toPoint();
    updateHoveredIndex();
    QTreeView::mouseMoveEvent(event);
}

void NavigationTreeView::scrollContentsBy(int dx, int dy) {
    QTreeView::scrollContentsBy(dx, dy);
    updateHoveredIndex();
}

void NavigationTreeView::updateHoveredIndex() {
    const QModelIndex hovered =
        hoveredPosition_.x() >= 0 ? indexAt(hoveredPosition_) : QModelIndex{};
    if (hoveredIndex_ != hovered) {
        hoveredIndex_ = hovered;
        viewport()->update();
    }
}

bool NavigationTreeView::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::Leave) {
        hoveredPosition_ = {-1, -1};
        if (hoveredIndex_.isValid()) {
            hoveredIndex_ = QModelIndex{};
            viewport()->update();
        }
    }
    return QTreeView::viewportEvent(event);
}

void NavigationTreeView::drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    const bool selected = selectionModel() && selectionModel()->isSelected(index);
    const bool hovered = hoveredIndex_ == index;
    if (!selected && !hovered) {
        QTreeView::drawRow(painter, option, index);
        return;
    }

    const auto theme = resolvedThemeForWidget(*this);
    const auto& colors = theme.colors;
    const QRectF highlight(5, option.rect.top() + 2, viewport()->width() - 10,
                           option.rect.height() - 4);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(theme.forcedContrast && selected ? QPen(colors.focus, 2) : QPen(Qt::NoPen));
    painter->setBrush(colors.muted);
    painter->drawRoundedRect(highlight, 5, 5);
    painter->restore();

    QStyleOptionViewItem unselected(option);
    unselected.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);
    QTreeView::drawRow(painter, unselected, index);
}

} // namespace choscordb::design
