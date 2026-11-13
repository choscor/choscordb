#pragma once

class QPainter;
class QStyleOption;
class QWidget;

namespace choscordb::design::detail {
bool drawSwitchIndicator(const QStyleOption* option, QPainter* painter, const QWidget* widget);
}
