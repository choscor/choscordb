#pragma once

#include "design_system/theme.h"
#include <QString>
#include <QStringView>

namespace choscordb::design {
[[nodiscard]] QString resolveStyleTokens(QString sheet, const ResolvedTheme& theme);
[[nodiscard]] QString themedStyleSheet(QStringView relativePath, const ResolvedTheme& theme);
}
