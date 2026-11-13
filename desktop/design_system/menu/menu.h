#pragma once

class QWidget;
class QEvent;
class QPoint;
class QMenu;
class QAction;
class QAbstractButton;

namespace choscordb::design {
void popupContextMenu(QMenu& menu, const QPoint& cursor);
QAction* execContextMenu(QMenu& menu, const QPoint& cursor);
}

namespace choscordb::design::detail {
int menuShadowMargin();
QPoint contextMenuPosition(const QPoint& cursor);
QAbstractButton* owningButton(QWidget* origin, const QMenu* menu);
void polishMenu(QWidget* widget);
void prepareStandardContextMenu(QWidget* widget, QEvent* event);
void positionSubmenu(QWidget* widget, QEvent* event);
}
