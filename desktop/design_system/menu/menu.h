#pragma once

class QWidget;
class QEvent;
class QPoint;
class QMenu;
class QAction;

namespace choscordb::design {
// Open at the invoking event's global position, constrained to the owner window.
// Both entry points share embedded presentation and outside-click dismissal.
void popupContextMenu(QMenu& menu, const QPoint& cursor);
QAction* execContextMenu(QMenu& menu, const QPoint& cursor);
} // namespace choscordb::design

namespace choscordb::design::detail {
int menuShadowMargin();
QPoint contextMenuPosition(const QPoint& cursor);
void polishMenu(QWidget* widget);
void prepareStandardContextMenu(QWidget* widget, QEvent* event);
void positionSubmenu(QWidget* widget, QEvent* event);
} // namespace choscordb::design::detail
