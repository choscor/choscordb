#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/icons.h"
#include "design_system/theme.h"
#include <QPainter>

namespace choscordb::design {
QSize NavigationProfileDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {180, dimension(Dimension::NavigationRow)};
}
void NavigationProfileDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
    if (!option.widget)
        return;
    const auto colors = resolvedThemeForWidget(*option.widget).colors;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const auto bounds = option.rect.adjusted(1, 0, -1, 0);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(selected ? colors.sidebarBorder : Qt::transparent);
    painter->setBrush(selected ? colors.sidebarAccent : hovered ? colors.muted : colors.sidebar);
    painter->drawRoundedRect(bounds, 6, 6);
    const QRect badge(bounds.left() + 7, bounds.center().y() - 13, 26, 27);
    const bool sqlite = index.data(DriverRole).toString() == "sqlite";
    painter->setPen(sqlite ? colors.sqliteBadgeBorder : colors.postgresBadgeBorder);
    painter->setBrush(sqlite ? colors.sqliteBadgeBackground : colors.postgresBadgeBackground);
    painter->drawRoundedRect(badge, 6, 6);
    themedIcon(driverIcon(index.data(DriverRole).toString()),
               sqlite ? colors.sqliteBadgeForeground : colors.postgresBadgeForeground, 16)
        .paint(painter, badge.adjusted(5, 6, -6, -7));
    const auto title = index.data(Qt::DisplayRole).toString().section('\n', 0, 0);
    const int textLeft = badge.right() + 8;
    const int textWidth = qMax(0, bounds.right() - textLeft - 25);
    auto titleFont = resolveTypography(TypographyRole::Field);
    painter->setFont(titleFont);
    painter->setPen(colors.text);
    painter->drawText(QRect(textLeft, bounds.top(), textWidth, bounds.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, textWidth));
    if (selected)
        themedIcon(Icon::Check, colors.sidebarForeground, 14)
            .paint(painter, QRect(bounds.right() - 21, bounds.center().y() - 7, 14, 14));
    if (option.state & QStyle::State_HasFocus) {
        painter->setPen(QPen(colors.focus, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 6, 6);
    }
    painter->restore();
}
} // namespace choscordb::design
