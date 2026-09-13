#pragma once

#include "design_system/theme.h"
#include <QApplication>
#include <QWidget>

namespace choscordb::design::detail {
inline QVariant scopedThemeValue(const QWidget* widget) {
    for (auto* ancestor = widget; ancestor; ancestor = ancestor->parentWidget()) {
        const auto value = ancestor->property("designTheme");
        if (value.canConvert<ResolvedTheme>()) {
            return value;
        }
    }
    return qApp->property("designTheme");
}
} // namespace choscordb::design::detail
