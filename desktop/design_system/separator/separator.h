#pragma once

#include "design_system/colors/colors.h"
#include <optional>

class QFrame;
class QWidget;

namespace choscordb::design {
QFrame* createToolbarSeparator(QWidget* parent);
QFrame* createToneLine(QWidget* parent);
void setToneLine(QFrame* line, std::optional<StatusTone> tone);
}
