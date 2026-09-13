#include "design_system/dock/dock_style.h"

namespace choscordb::design {

QString dockApplicationStyleSheet() {
    return QStringLiteral(R"(QDockWidget::title { min-height: %14px; padding-left: %5px; }
)");
}
} // namespace choscordb::design
