#include "design_system/field/focus_indicator.h"
#include "design_system/theme.h"
#include <QPainter>
#include <QPushButton>
#include <QStyleOption>

namespace choscordb::design::detail {
bool drawFocusIndicator(QStyle::PrimitiveElement element, const QStyleOption* option,
                        QPainter* painter, const QWidget* widget) {
    if (element == QStyle::PE_FrameFocusRect) {
        if (option->state.testFlag(QStyle::State_KeyboardFocusChange)) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            auto color = option->palette.color(QPalette::Dark);
            if (const auto* button = qobject_cast<const QPushButton*>(widget)) {
                const auto variant = button->property("variant").toString();
                const bool primary = variant == QLatin1String("default") ||
                                     (variant != QLatin1String("outline") &&
                                      variant != QLatin1String("destructive") &&
                                      (button->isDefault() || button->isChecked() ||
                                       button->property("primary").toBool()));
                if (primary)
                    color = resolvedThemeForWidget(*button).colors.primaryForeground;
            }
            painter->setPen(QPen(color, focusSpec().ringWidth));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(QRectF(option->rect).adjusted(1.5, 1.5, -1.5, -1.5), 6, 6);
            painter->restore();
        }
        return true;
    }
    return false;
}

} // namespace choscordb::design::detail
