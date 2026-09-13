#pragma once

#include <QFont>
#include <QString>

namespace choscordb::design {

enum class TypographyRole { Ui, Monospace, Small, Heading, Base, DialogTitle, Field };

struct TypographySpec final {
    QString family;
    int pixelSize = 14;
    int lineHeight = 20;
    QFont::Weight weight = QFont::Normal;
};

[[nodiscard]] TypographySpec typographySpec(TypographyRole role);
[[nodiscard]] bool bundledFontsAvailable();
[[nodiscard]] QFont resolveTypography(TypographyRole role);

} // namespace choscordb::design
