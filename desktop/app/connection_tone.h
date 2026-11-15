#pragma once

#include "design_system/colors/colors.h"

#include <QString>
#include <optional>

namespace choscordb {
inline std::optional<design::StatusTone> connectionTone(const QString& code) {
    if (code == QLatin1String("neutral"))
        return design::StatusTone::Neutral;
    if (code == QLatin1String("brand"))
        return design::StatusTone::Brand;
    if (code == QLatin1String("success"))
        return design::StatusTone::Success;
    if (code == QLatin1String("warning"))
        return design::StatusTone::Warning;
    if (code == QLatin1String("danger"))
        return design::StatusTone::Danger;
    return std::nullopt;
}
}
