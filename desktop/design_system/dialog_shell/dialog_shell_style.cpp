#include "design_system/dialog_shell/dialog_shell_style.h"
#include "design_system/style/style_resource.h"

namespace choscordb::design {

QString dialogShellApplicationStyleSheet() {
    return loadStyleSheet(QStringLiteral("dialog_shell/dialog_shell_application_style_sheet.qss"));
}
} // namespace choscordb::design
