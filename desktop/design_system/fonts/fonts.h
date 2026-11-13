#pragma once

#include <QFont>
#include <QString>

namespace choscordb::design {

enum class TextStyle { LargeTitle, Title1, Title2, Title3, Headline, Body, Callout, Caption };

enum class FontDesign { Default, Monospaced };

struct TypographySpec final {
    QString family;
    int pixelSize = 13;
    int lineHeight = 16;
    QFont::Weight weight = QFont::Normal;
};

[[nodiscard]] TypographySpec typographySpec(TextStyle style,
                                            FontDesign design = FontDesign::Default);
[[nodiscard]] bool bundledFontsAvailable();
[[nodiscard]] QFont resolveTypography(TextStyle style, FontDesign design = FontDesign::Default);
[[nodiscard]] QFont resolveSectionLabelTypography();

}
