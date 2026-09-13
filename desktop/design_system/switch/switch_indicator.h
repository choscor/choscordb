#pragma once

class QPainter;
class QStyleOption;
class QWidget;

namespace choscordb::design::detail {
// Paint inside the checkbox indicator's saved painter state.
bool drawSwitchIndicator(const QStyleOption* option, QPainter* painter, const QWidget* widget);
} // namespace choscordb::design::detail
