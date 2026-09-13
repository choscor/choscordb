#include "design_system/checkbox/checkbox_indicator.h"
#include "design_system/style/scoped_theme.h"
#include "design_system/switch/switch_indicator.h"
#include "design_system/theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

namespace choscordb::design::detail {
bool drawCheckboxIndicator(QStyle::PrimitiveElement element, const QStyleOption* option,
                           QPainter* painter, const QWidget* widget) {
    if (element == QStyle::PE_IndicatorCheckBox ||
        element == QStyle::PE_IndicatorItemViewItemCheck) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const bool enabled = option->state.testFlag(QStyle::State_Enabled);
        const bool checked = option->state.testFlag(QStyle::State_On);
        const bool mixed = option->state.testFlag(QStyle::State_NoChange);
        if (!enabled) {
            painter->setOpacity(0.5);
        }
        const auto rectangle = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        auto fill = option->palette.color(QPalette::Active, QPalette::Accent);
        auto markColor = option->palette.color(QPalette::Active, QPalette::HighlightedText);
        auto border = option->palette.color(QPalette::Mid);
        const auto themeValue = scopedThemeValue(widget);
        if (themeValue.canConvert<ResolvedTheme>()) {
            const auto colors = themeValue.value<ResolvedTheme>().colors;
            fill = colors.primary;
            markColor = colors.primaryForeground;
            border = colors.input;
        }
        if (drawSwitchIndicator(option, painter, widget)) {
            painter->restore();
            return true;
        }
        painter->setPen(QPen(checked || mixed ? fill : border, 1));
        painter->setBrush(checked || mixed ? QBrush(fill) : Qt::NoBrush);
        painter->drawRoundedRect(rectangle, 4, 4);
        if (checked || mixed) {
            painter->setPen(QPen(markColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const auto origin = QPointF(option->rect.topLeft());
            if (mixed) {
                painter->drawLine(origin + QPointF(4, 8), origin + QPointF(12, 8));
            } else {
                QPainterPath mark;
                mark.moveTo(origin + QPointF(3.5, 8));
                mark.lineTo(origin + QPointF(6.5, 11));
                mark.lineTo(origin + QPointF(12.5, 5));
                painter->drawPath(mark);
            }
        }
        painter->restore();
        return true;
    }
    return false;
}

} // namespace choscordb::design::detail
