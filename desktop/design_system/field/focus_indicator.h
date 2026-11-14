#pragma once

#include <QStyle>

class QPainter;
class QRectF;
class QStyleOption;
class QWidget;

namespace choscordb::design {
[[nodiscard]] bool showsKeyboardFocus(const QStyleOption& option, const QWidget* widget);
void paintRowFocusRing(QPainter& painter, const QStyleOption& option, const QWidget* widget,
                       const QRectF& bounds, qreal cornerRadius);
}

namespace choscordb::design::detail {
bool drawFocusIndicator(QStyle::PrimitiveElement element, const QStyleOption* option,
                        QPainter* painter, const QWidget* widget);
}
