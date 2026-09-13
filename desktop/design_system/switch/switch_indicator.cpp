#include "design_system/switch/switch_indicator.h"
#include "design_system/theme.h"

#include <QPainter>
#include <QStyleOption>
#include <QWidget>

namespace choscordb::design::detail {
bool drawSwitchIndicator(const QStyleOption* option, QPainter* painter, const QWidget* widget) {
    const bool checked = option->state.testFlag(QStyle::State_On);
    const auto rectangle = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
    if (widget && widget->property("designRole").toString() == QLatin1String("switch")) {
        const auto theme = resolvedThemeForWidget(*widget);
        const auto& colors = theme.colors;
        painter->setPen(theme.forcedContrast ? QPen(colors.border, 1) : Qt::NoPen);
        painter->setBrush(checked ? colors.primary : colors.switchTrack);
        painter->drawRoundedRect(rectangle, rectangle.height() / 2, rectangle.height() / 2);
        const auto thumbSize = dimension(Dimension::SwitchThumb);
        const auto inset = (dimension(Dimension::SwitchHeight) - thumbSize) / 2;
        const bool right = checked != (option->direction == Qt::RightToLeft);
        const auto x =
            right ? option->rect.right() - inset - thumbSize + 1 : option->rect.left() + inset;
        painter->setPen(Qt::NoPen);
        painter->setBrush(theme.forcedContrast ? (checked ? colors.primaryForeground : colors.text)
                                               : QColor(Qt::white));
        painter->drawEllipse(QRectF(x, option->rect.top() + inset, thumbSize, thumbSize));
        return true;
    }
    return false;
}
} // namespace choscordb::design::detail
