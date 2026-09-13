#pragma once

#include <QPointer>
class QFocusFrame;
class QWidget;
class QEvent;

namespace choscordb::design::detail {
void handleFieldFocusEvent(QWidget* widget, QEvent* event, QPointer<QFocusFrame>& focusFrame);
} // namespace choscordb::design::detail
