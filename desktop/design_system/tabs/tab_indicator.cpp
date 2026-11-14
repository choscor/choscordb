#include "design_system/tabs/tab_indicator.h"
#include "design_system/icons.h"
#include "design_system/theme.h"
#include <QPainter>
#include <QStyleOption>
#include <QWidget>

namespace choscordb::design::detail {
bool drawTabIndicator(QStyle::PrimitiveElement element, const QStyleOption* option,
                      QPainter* painter, const QWidget* widget) {
    if ((element == QStyle::PE_IndicatorTabTearLeft ||
         element == QStyle::PE_IndicatorTabTearRight) &&
        widget && widget->property("designTabVariant").toString() == "document")
        return true;
    if (element == QStyle::PE_IndicatorTabClose) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const auto colors = widget ? resolvedThemeForWidget(*widget).colors
                                   : resolveColors(ResolvedAppearance::Light);
        const bool enabled = option->state.testFlag(QStyle::State_Enabled);
        const bool pressed = enabled && option->state.testFlag(QStyle::State_Sunken);
        const bool hover = enabled && option->state.testFlag(QStyle::State_Raised);
        const auto rounding = radius(Radius::Small);
        if (pressed || hover) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(colors.tint);
            painter->drawRoundedRect(QRectF(option->rect), rounding, rounding);
        }
        const auto color = !enabled           ? colors.textInactive
                           : hover || pressed ? colors.textDefault
                                              : colors.textSubtle;
        const int icon = dimension(Dimension::IconSmall);
        const auto mark = QRect(QPoint(), QSize(icon, icon));
        themedIcon(Icon::Close, color, icon)
            .paint(painter, mark.translated(option->rect.center() - mark.center()));
        painter->restore();
        return true;
    }
    return false;
}

}
