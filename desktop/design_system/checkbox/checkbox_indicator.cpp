#include "design_system/checkbox/checkbox_indicator.h"
#include "design_system/style/scoped_theme.h"
#include "design_system/switch/switch_indicator.h"
#include "design_system/theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

namespace choscordb::design::detail {
namespace {
struct IndicatorColors final {
    QColor rest;
    QColor checked;
    QColor ring;
    QColor checkedRing;
    QColor mark;
};

IndicatorColors indicatorColors(const QWidget* widget, bool radio) {
    const auto themeValue = scopedThemeValue(widget);
    const auto colors = themeValue.canConvert<ResolvedTheme>()
                            ? themeValue.value<ResolvedTheme>().colors
                            : resolveColors(ResolvedAppearance::Light);
    const auto ring = radio ? colors.line : colors.hairline;
    return {colors.base, colors.contrast, ring, radio ? ring : colors.contrast,
            radio ? colors.base : colors.textInverse};
}

void paintKeyboardFocus(const QStyleOption* option, QPainter* painter, const QRectF& shape,
                        qreal rounding, const QWidget* widget) {
    if (!option->state.testFlag(QStyle::State_HasFocus) ||
        !option->state.testFlag(QStyle::State_KeyboardFocusChange))
        return;
    const auto themeValue = scopedThemeValue(widget);
    const auto ring = themeValue.canConvert<ResolvedTheme>()
                          ? themeValue.value<ResolvedTheme>().colors.brand
                          : option->palette.color(QPalette::Accent);
    const qreal width = focusSpec().ringWidth;
    painter->setPen(QPen(ring, width));
    painter->setBrush(Qt::NoBrush);
    const qreal offset = width / 2;
    const auto outer = shape.adjusted(-offset, -offset, offset, offset);
    painter->drawRoundedRect(outer, rounding + offset, rounding + offset);
}
}

bool drawCheckboxIndicator(QStyle::PrimitiveElement element, const QStyleOption* option,
                           QPainter* painter, const QWidget* widget) {
    const bool enabled = option->state.testFlag(QStyle::State_Enabled);
    if (element == QStyle::PE_IndicatorRadioButton) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (!enabled)
            painter->setOpacity(0.5);
        const bool checked = option->state.testFlag(QStyle::State_On);
        const auto colors = indicatorColors(widget, true);
        const auto circle = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->setPen(QPen(checked ? colors.checkedRing : colors.ring, 1));
        painter->setBrush(checked ? colors.checked : colors.rest);
        painter->drawEllipse(circle);
        if (checked) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(colors.mark);
            const qreal dot = QRectF(option->rect).width() / 4;
            painter->drawEllipse(QRectF(option->rect).center(), dot, dot);
        }
        paintKeyboardFocus(option, painter, QRectF(option->rect), option->rect.width() / 2.0,
                           widget);
        painter->restore();
        return true;
    }
    if (element == QStyle::PE_IndicatorCheckBox ||
        element == QStyle::PE_IndicatorItemViewItemCheck) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (drawSwitchIndicator(option, painter, widget)) {
            painter->restore();
            return true;
        }
        if (!enabled)
            painter->setOpacity(0.5);
        const bool on = option->state.testFlag(QStyle::State_On) ||
                        option->state.testFlag(QStyle::State_NoChange);
        const bool mixed = option->state.testFlag(QStyle::State_NoChange);
        const auto colors = indicatorColors(widget, false);
        const auto rectangle = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal rounding = radius(Radius::Small);
        painter->setPen(QPen(on ? colors.checkedRing : colors.ring, 1));
        painter->setBrush(on ? colors.checked : colors.rest);
        painter->drawRoundedRect(rectangle, rounding, rounding);
        if (on) {
            const QRectF box(option->rect);
            const qreal glyph = box.width() * 0.75;
            const QPointF origin = box.center() - QPointF(glyph / 2, glyph / 2);
            const auto at = [&](qreal x, qreal y) {
                return origin + QPointF(glyph * x / 256, glyph * y / 256);
            };
            painter->setPen(
                QPen(colors.mark, glyph * 24 / 256, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            if (mixed) {
                painter->drawLine(at(40, 128), at(216, 128));
            } else {
                QPainterPath mark;
                mark.moveTo(at(40, 144));
                mark.lineTo(at(96, 200));
                mark.lineTo(at(224, 72));
                painter->drawPath(mark);
            }
        }
        paintKeyboardFocus(option, painter, QRectF(option->rect), rounding, widget);
        painter->restore();
        return true;
    }
    return false;
}

}
