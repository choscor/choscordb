#include "design_system/control_glyphs/arrow_indicator.h"
#include "design_system/theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

namespace choscordb::design::detail {
bool drawArrowIndicator(QStyle::PrimitiveElement element, const QStyleOption* option,
                        QPainter* painter, const QWidget*) {
    if (element == QStyle::PE_IndicatorArrowDown || element == QStyle::PE_IndicatorArrowUp ||
        element == QStyle::PE_IndicatorArrowLeft || element == QStyle::PE_IndicatorArrowRight ||
        element == QStyle::PE_IndicatorSpinDown || element == QStyle::PE_IndicatorSpinUp) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(QRectF(option->rect).center());
        if (element == QStyle::PE_IndicatorArrowUp || element == QStyle::PE_IndicatorSpinUp) {
            painter->rotate(180);
        } else if (element == QStyle::PE_IndicatorArrowLeft) {
            painter->rotate(90);
        } else if (element == QStyle::PE_IndicatorArrowRight) {
            painter->rotate(-90);
        }
        if (!option->state.testFlag(QStyle::State_Enabled)) {
            painter->setOpacity(0.5);
        }
        painter->setPen(QPen(option->palette.color(QPalette::ButtonText), 1.5, Qt::SolidLine,
                             Qt::RoundCap, Qt::RoundJoin));
        QPainterPath chevron;
        chevron.moveTo(-4, -2);
        chevron.lineTo(0, 2);
        chevron.lineTo(4, -2);
        painter->drawPath(chevron);
        painter->restore();
        return true;
    }
    return false;
}

} // namespace choscordb::design::detail
