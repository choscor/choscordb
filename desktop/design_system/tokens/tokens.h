#pragma once

#include "design_system/colors/colors.h"
#include <QList>
#include <QString>

namespace choscordb::design {

struct DesignToken final {
    QString name;
    QString value;
    QString source;
};

[[nodiscard]] QList<DesignToken> designTokens(ResolvedAppearance appearance);

} // namespace choscordb::design
