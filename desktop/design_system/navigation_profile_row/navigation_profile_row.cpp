#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/icons.h"
#include "design_system/theme.h"
#include <QPainter>

namespace choscordb::design {
QSize NavigationProfileDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {180, dimension(Dimension::Row)};
}
void NavigationProfileDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
    if (!option.widget)
        return;
    const auto theme = resolvedThemeForWidget(*option.widget);
    const auto& colors = theme.colors;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const auto bounds = option.rect.adjusted(1, 0, -1, 0);
    const int rowRadius = radius(Radius::Medium);
    const int inset = spacing(Spacing::Two);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(selected ? colors.border : Qt::transparent);
    painter->setBrush(selected  ? colors.selection
                      : hovered ? colors.surfaceRaised
                                : colors.sidebar);
    painter->drawRoundedRect(bounds, rowRadius, rowRadius);
    // One neutral tile beside the unmodified driver logo.
    const int tileSize = dimension(Dimension::ControlExtraSmall);
    const QRect tile(bounds.left() + inset, bounds.center().y() - tileSize / 2 + 1, tileSize,
                     tileSize);
    painter->setPen(colors.border);
    painter->setBrush(colors.surfaceRaised);
    painter->drawRoundedRect(QRectF(tile).adjusted(.5, .5, -.5, -.5), radius(Radius::Small),
                             radius(Radius::Small));
    const int iconSize = dimension(Dimension::Icon);
    const int iconInset = (tileSize - iconSize) / 2;
    themedIcon(driverIcon(index.data(DriverRole).toString()), colors.fg, iconSize)
        .paint(painter, tile.adjusted(iconInset, iconInset, -iconInset, -iconInset));
    const auto title = index.data(Qt::DisplayRole).toString().section('\n', 0, 0);
    const int checkSize = dimension(Dimension::IconSmall);
    const int textLeft = tile.right() + 1 + inset;
    const int textWidth = qMax(0, bounds.right() - textLeft - checkSize - 2 * inset);
    auto titleFont = resolveTypography(TypographyRole::Field);
    painter->setFont(titleFont);
    painter->setPen(colors.fg);
    painter->drawText(QRect(textLeft, bounds.top(), textWidth, bounds.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, textWidth));
    if (selected)
        themedIcon(Icon::Check, colors.fg, checkSize)
            .paint(painter, QRect(bounds.right() - inset - checkSize,
                                  bounds.center().y() - checkSize / 2 + 1, checkSize, checkSize));
    if (option.state & QStyle::State_HasFocus) {
        painter->setPen(QPen(colors.ring, focusSpec().ringWidth));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(bounds).adjusted(1, 1, -1, -1), rowRadius, rowRadius);
    }
    painter->restore();
}
} // namespace choscordb::design
