#include "design_system/scrollbar/scrollbar_style.h"

namespace choscordb::design {
QString scrollbarStyleSheet() {
    return QStringLiteral(
        R"(QScrollBar:vertical { width: 10px; margin: 0; border: 0; background: transparent; }
QScrollBar:horizontal { height: 10px; margin: 0; border: 0; background: transparent; }
QScrollBar::handle { background: @border; border: 2px solid transparent; border-radius: 5px; min-width: 20px; min-height: 20px; }
QScrollBar::handle:hover, QScrollBar::handle:pressed { background: @mutedText; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: 0; background: transparent; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)");
}
} // namespace choscordb::design
