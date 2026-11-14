#pragma once

class QWidget;
class QEvent;
class QRect;
class QString;

namespace choscordb::design::detail {
void showTooltip(QWidget* owner, const QRect& anchor, const QString& text);
bool handleTooltipEvent(QWidget* widget, QEvent* event);
}
