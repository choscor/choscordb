#pragma once

#include <QString>
class QDockWidget;

namespace choscordb::design {
struct ResolvedTheme;
void styleDockWidget(QDockWidget& dock, const ResolvedTheme& theme);
} // namespace choscordb::design
