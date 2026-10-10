#pragma once

#include "design_system/theme.h"
#include <QString>
#include <QStringView>

namespace choscordb::design {
// Replace every named `@token` in a stylesheet with its theme value. Unknown
// tokens are reported and left in place.
[[nodiscard]] QString resolveStyleTokens(QString sheet, const ResolvedTheme& theme);
// Load a bundled stylesheet and resolve its tokens.
[[nodiscard]] QString themedStyleSheet(QStringView relativePath, const ResolvedTheme& theme);
} // namespace choscordb::design
