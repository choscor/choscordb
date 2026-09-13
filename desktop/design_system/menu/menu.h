#pragma once

class QWidget;
class QEvent;

namespace choscordb::design::detail {
int menuShadowMargin();
void polishMenu(QWidget* widget);
void positionSubmenu(QWidget* widget, QEvent* event);
} // namespace choscordb::design::detail
