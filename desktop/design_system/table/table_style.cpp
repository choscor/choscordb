#include "design_system/table/table_style.h"

namespace choscordb::design {
QString tableStyleSheet() {
    return QStringLiteral(R"(QTableView { font-size: 12px; }
)");
}

QString tableItemStyleSheet() {
    return QStringLiteral(R"(QTableView::item { padding: 0 12px; border-bottom: 1px solid @border; }
)");
}
} // namespace choscordb::design
