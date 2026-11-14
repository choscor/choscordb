#pragma once

class QAbstractItemView;
class QComboBox;
class QEvent;

namespace choscordb::design::detail {
void prepareComboPopup(QComboBox& combo);
void preparePopoverList(QAbstractItemView& popup);
bool handleFontComboResize(QComboBox& combo, QEvent* event);
}
