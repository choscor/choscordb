#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/field/focus_indicator.h"
#include "design_system/icons.h"
#include "design_system/theme.h"
#include <QPainter>

namespace choscordb::design {
QSize NavigationProfileDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {dimension(Dimension::TableColumn), dimension(Dimension::Row)};
}
void NavigationProfileDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
    if (!option.widget)
        return;
    const auto theme = resolvedThemeForWidget(*option.widget);
    const auto& colors = theme.colors;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const int sideInset = spacing(Spacing::One);
    const auto bounds = option.rect.adjusted(sideInset, 0, -sideInset, 0);
    const qreal gap = layoutMetrics().navigationHighlightGap;
    const auto highlight = QRectF(bounds).adjusted(0, gap / 2, 0, -gap / 2);
    const int rowRadius = radius(Radius::Large);
    const int inset = spacing(Spacing::Three);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const bool enabled = option.state & QStyle::State_Enabled;
    painter->setPen(Qt::NoPen);
    if (selected || hovered) {
        painter->setBrush(colors.tint);
        painter->drawRoundedRect(highlight, rowRadius, rowRadius);
    }
    const int iconSize = dimension(Dimension::Icon);
    const QRect icon(bounds.left() + inset, bounds.center().y() - iconSize / 2 + 1, iconSize,
                     iconSize);
    themedIcon(driverIcon(index.data(DriverRole).toString()), colors.textDefault, iconSize)
        .paint(painter, icon);
    const auto title = index.data(Qt::DisplayRole).toString().section('\n', 0, 0);
    const int textLeft = icon.right() + 1 + spacing(Spacing::TwoHalf);
    const auto tone = index.data(ToneRole);
    const int marker = spacing(Spacing::Two);
    const int textRight = bounds.right() - inset - (tone.isValid() ? marker + inset : 0);
    const int textWidth = qMax(0, textRight - textLeft);
    if (tone.isValid()) {
        painter->setBrush(statusToneColor(colors, StatusTone(tone.toInt())));
        painter->drawEllipse(QRect(bounds.right() - inset - marker + 1,
                                   bounds.center().y() - marker / 2 + 1, marker, marker));
    }
    auto titleFont = resolveTypography(TextStyle::Body);
    titleFont.setWeight(QFont::Medium);
    painter->setFont(titleFont);
    painter->setPen(enabled ? colors.textDefault : colors.textInactive);
    painter->drawText(QRect(textLeft, bounds.top(), textWidth, bounds.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, textWidth));
    paintRowFocusRing(*painter, option, option.widget, highlight, rowRadius);
    painter->restore();
}
}
