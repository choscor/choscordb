#pragma once

#include <QString>
class QDockWidget;

namespace choscordb::design {
struct ResolvedTheme;
QString dockApplicationStyleSheet();
void styleDockWidget(QDockWidget& dock, const ResolvedTheme& theme);
} // namespace choscordb::design
