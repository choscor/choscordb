#pragma once

#include <QStyle>

class QPainter;
class QStyleOption;
class QWidget;

namespace choscordb::design::detail {
bool drawTabIndicator(QStyle::PrimitiveElement element, const QStyleOption* option,
                      QPainter* painter, const QWidget* widget);
} // namespace choscordb::design::detail
