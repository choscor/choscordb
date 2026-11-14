#include "design_system/switch/switch_indicator.h"
#include "design_system/theme.h"

#include <QPainter>
#include <QStyleOption>
#include <QWidget>

namespace choscordb::design::detail {
bool drawSwitchIndicator(const QStyleOption* option, QPainter* painter, const QWidget* widget) {
    if (!widget || widget->property("designRole").toString() != QLatin1String("switch"))
        return false;
    const bool checked = option->state.testFlag(QStyle::State_On);
    const bool enabled = option->state.testFlag(QStyle::State_Enabled);
    const auto theme = resolvedThemeForWidget(*widget);
    const auto& colors = theme.colors;
    const auto fill = switchColors(colors, theme.appearance, theme.forcedContrast, checked);
    const qreal rounding = dimension(Dimension::SwitchRadius);
    if (!enabled)
        painter->setOpacity(0.5);
    const QRectF track(option->rect);
    painter->setPen(QPen(fill.ring, 1));
    painter->setBrush(fill.track);
    painter->drawRoundedRect(track.adjusted(0.5, 0.5, -0.5, -0.5), rounding - 0.5, rounding - 0.5);
    const auto thumbWidth = dimension(Dimension::SwitchThumb);
    const bool right = checked != (option->direction == Qt::RightToLeft);
    const QRectF thumb(right ? track.right() + 1 - thumbWidth : track.left(), track.top(),
                       thumbWidth, track.height());
    painter->setPen(Qt::NoPen);
    painter->setBrush(colors.shadowDrop);
    painter->drawRoundedRect(thumb.translated(0, 1), rounding, rounding);
    painter->setPen(QPen(colors.shadowEdge, 1));
    painter->setBrush(fill.thumb);
    painter->drawRoundedRect(thumb.adjusted(0.5, 0.5, -0.5, -0.5), rounding - 0.5, rounding - 0.5);
    if (option->state.testFlag(QStyle::State_HasFocus) &&
        option->state.testFlag(QStyle::State_KeyboardFocusChange)) {
        const qreal width = focusSpec().ringWidth;
        painter->setPen(QPen(colors.brand, width));
        painter->setBrush(Qt::NoBrush);
        const qreal offset = width / 2;
        painter->drawRoundedRect(track.adjusted(-offset, -offset, offset, offset),
                                 rounding + offset, rounding + offset);
    }
    return true;
}
}
