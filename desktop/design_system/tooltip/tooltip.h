#pragma once

#include <QPointer>
class QWidget;
class QEvent;

namespace choscordb::design::detail {
bool handleTooltipEvent(QWidget* widget, QEvent* event, QPointer<QWidget>& tooltip,
                        QPointer<QWidget>& owner);
} // namespace choscordb::design::detail
