#pragma once

class QComboBox;
class QEvent;

namespace choscordb::design::detail {
void prepareComboPopup(QComboBox& combo);
bool handleFontComboResize(QComboBox& combo, QEvent* event);
} // namespace choscordb::design::detail
