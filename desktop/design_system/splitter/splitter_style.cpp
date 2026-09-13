#include "design_system/splitter/splitter_style.h"

namespace choscordb::design {
QString splitterStyleSheet() {
    return QStringLiteral(R"(QSplitter::handle { background: @border; }
QSplitter::handle:hover, QSplitter::handle:pressed { background: @focus; }
)");
}
} // namespace choscordb::design
