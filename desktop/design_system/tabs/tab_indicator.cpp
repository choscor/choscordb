#include "design_system/tabs/tab_indicator.h"
#include "design_system/theme.h"
#include <QPainter>
#include <QStyleOption>
#include <QWidget>

namespace choscordb::design::detail {
bool drawTabIndicator(QStyle::PrimitiveElement element, const QStyleOption* option,
                      QPainter* painter, const QWidget* widget) {
    // Scroll buttons already signal overflow. Fusion's torn edge overlaps the
    // first/last visible document with a jagged line in the accent color.
    if ((element == QStyle::PE_IndicatorTabTearLeft ||
         element == QStyle::PE_IndicatorTabTearRight) &&
        widget && widget->property("designTabVariant").toString() == "document")
        return true;
    if (element == QStyle::PE_IndicatorTabClose) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const auto color = widget ? resolvedThemeForWidget(*widget).colors.foreground
                                  : option->palette.color(QPalette::ButtonText);
        painter->setOpacity(option->state.testFlag(QStyle::State_Enabled) ? 0.7 : 0.35);
        painter->translate(QRectF(option->rect).center());
        painter->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(-3, -3), QPointF(3, 3));
        painter->drawLine(QPointF(-3, 3), QPointF(3, -3));
        painter->restore();
        return true;
    }
    return false;
}

} // namespace choscordb::design::detail
