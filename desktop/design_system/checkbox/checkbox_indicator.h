#pragma once

#include <QStyle>

class QPainter;
class QStyleOption;
class QWidget;

namespace choscordb::design::detail {
bool drawCheckboxIndicator(QStyle::PrimitiveElement element, const QStyleOption* option,
                           QPainter* painter, const QWidget* widget);
} // namespace choscordb::design::detail
