#pragma once

class QWidget;
class QEvent;

namespace choscordb::design::detail {
void polishControlGlyphs(QWidget* widget);
void unpolishControlGlyphs(QWidget* widget);
void updateControlGlyphs(QWidget* widget, QEvent* event);
} // namespace choscordb::design::detail
