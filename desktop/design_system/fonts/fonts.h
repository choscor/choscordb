#pragma once

#include <QFont>
#include <QString>

namespace choscordb::design {

enum class TypographyRole {
    Ui,
    Monospace,
    Small,
    NavigationDetail,
    Heading,
    Base,
    DialogTitle,
    Field,
    SectionCaption,
    Metadata
};

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
