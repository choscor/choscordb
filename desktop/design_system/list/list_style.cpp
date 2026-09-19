#include "design_system/list/list_style.h"

namespace choscordb::design {
QString listStyleSheet() {
    return QStringLiteral(
        R"(QListView::item { min-height: 20px; padding: 4px 6px; border-radius: 0; }
)");
}
} // namespace choscordb::design
