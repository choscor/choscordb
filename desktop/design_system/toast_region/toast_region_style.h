#pragma once

#include <QString>

namespace choscordb::design {
struct ResolvedTheme;
QString toastRegionApplicationStyleSheet(const ResolvedTheme& theme);
} // namespace choscordb::design
