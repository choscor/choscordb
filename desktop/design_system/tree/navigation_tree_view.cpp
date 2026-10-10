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
        updateRow(hoveredIndex_);
        hoveredIndex_ = hovered;
        updateRow(hoveredIndex_);
    }
}

void NavigationTreeView::updateRow(const QModelIndex& index) {
    // The hover fill spans the viewport width, beyond the indented item rectangle.
    const QRect item = index.isValid() ? visualRect(index) : QRect{};
    if (item.isValid())
        viewport()->update(QRect(0, item.top(), viewport()->width(), item.height()));
}

bool NavigationTreeView::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::Leave) {
        hoveredPosition_ = {-1, -1};
        if (hoveredIndex_.isValid()) {
            updateRow(hoveredIndex_);
            hoveredIndex_ = QModelIndex{};
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
    const qreal gap = layoutMetrics().navigationHighlightGap;
    const int inset = spacing(Spacing::One);
    const int rounding = radius(Radius::Medium);
    const QRectF highlight(inset, option.rect.top() + gap / 2, viewport()->width() - 2 * inset,
                           option.rect.height() - gap);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(theme.forcedContrast && selected ? QPen(colors.ring, focusSpec().ringWidth)
                                                     : QPen(Qt::NoPen));
    painter->setBrush(selected ? colors.selection : colors.surfaceRaised);
    painter->drawRoundedRect(highlight, rounding, rounding);
    painter->restore();

    QStyleOptionViewItem unselected(option);
    unselected.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);
    QTreeView::drawRow(painter, unselected, index);
}

void NavigationTreeView::drawBranches(QPainter* painter, const QRect& rect,
                                      const QModelIndex& index) const {
    const bool selected = selectionModel() && selectionModel()->isSelected(index);
    if (selected || hoveredIndex_ == index) {
        const auto& colors = resolvedThemeForWidget(*this).colors;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setClipRect(rect);
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? colors.selection : colors.surfaceRaised);
        const qreal gap = layoutMetrics().navigationHighlightGap;
        const int inset = spacing(Spacing::One);
        const int rounding = radius(Radius::Medium);
        painter->drawRoundedRect(QRectF(inset, rect.top() + gap / 2,
                                        viewport()->width() - 2 * inset, rect.height() - gap),
                                 rounding, rounding);
        painter->restore();
    }
    QTreeView::drawBranches(painter, rect, index);
}

} // namespace choscordb::design
