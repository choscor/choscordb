#include "design_system/field/focus_indicator.h"
#include "design_system/theme.h"
#include <QPainter>
#include <QPushButton>
#include <QStyleOption>

namespace choscordb::design {
namespace {
QColor ringColor(const QStyleOption* option, const QWidget* widget) {
    return widget ? resolvedThemeForWidget(*widget).colors.brand
                  : option->palette.color(QPalette::Dark);
}
}
bool showsKeyboardFocus(const QStyleOption& option, const QWidget* widget) {
    return widget ? widget->window()->testAttribute(Qt::WA_KeyboardFocusChange)
                  : option.state.testFlag(QStyle::State_KeyboardFocusChange);
}
void paintRowFocusRing(QPainter& painter, const QStyleOption& option, const QWidget* widget,
                       const QRectF& bounds, qreal cornerRadius) {
    if (!option.state.testFlag(QStyle::State_HasFocus) || !showsKeyboardFocus(option, widget))
        return;
    const qreal ring = focusSpec().ringWidth;
    painter.save();
    painter.setPen(QPen(ringColor(&option, widget), ring));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(bounds.adjusted(ring / 2, ring / 2, -ring / 2, -ring / 2), cornerRadius,
                            cornerRadius);
    painter.restore();
}
}

namespace choscordb::design::detail {
bool drawFocusIndicator(QStyle::PrimitiveElement element, const QStyleOption* option,
                        QPainter* painter, const QWidget* widget) {
    if (element == QStyle::PE_FrameFocusRect) {
        if (showsKeyboardFocus(*option, widget)) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            auto color = ringColor(option, widget);
            if (const auto* button = qobject_cast<const QPushButton*>(widget)) {
                const auto variant = button->property("variant").toString();
                const bool primary = variant == QLatin1String("default") ||
                                     (variant != QLatin1String("outline") &&
                                      variant != QLatin1String("destructive") &&
                                      (button->isDefault() || button->isChecked() ||
                                       button->property("primary").toBool()));
                if (primary)
                    color = resolvedThemeForWidget(*button).colors.white;
            }
            painter->setPen(QPen(color, focusSpec().ringWidth));
            painter->setBrush(Qt::NoBrush);
            const qreal ring = focusSpec().ringWidth;
            painter->drawRoundedRect(
                QRectF(option->rect).adjusted(ring / 2, ring / 2, -ring / 2, -ring / 2),
                radius(Radius::Medium), radius(Radius::Medium));
            painter->restore();
        }
        return true;
    }
    return false;
}

}
