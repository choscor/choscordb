#pragma once

class QWidget;
class QEvent;
class QPoint;

namespace choscordb::design::detail {
int menuShadowMargin();
QPoint contextMenuPosition(const QPoint& cursor);
void polishMenu(QWidget* widget);
void positionSubmenu(QWidget* widget, QEvent* event);
} // namespace choscordb::design::detail
