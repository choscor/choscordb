#pragma once

#include "design_system/colors/colors.h"
#include "design_system/fonts/fonts.h"
#include "design_system/metrics/metrics.h"
#include "design_system/tokens/tokens.h"

class QWidget;

namespace choscordb::design {

struct ResolvedTheme final {
    ResolvedAppearance appearance = ResolvedAppearance::Light;
    SemanticColors colors;
    bool forcedContrast = false;

    friend bool operator==(const ResolvedTheme&, const ResolvedTheme&) = default;
};

[[nodiscard]] ResolvedTheme resolvedThemeForWidget(const QWidget& widget);
[[nodiscard]] QPalette applicationPalette(const ResolvedTheme& theme);
[[nodiscard]] QString applicationStyleSheet(const ResolvedTheme& theme,
                                            const DesignMetrics& metrics);

} // namespace choscordb::design

Q_DECLARE_METATYPE(choscordb::design::ResolvedTheme)
