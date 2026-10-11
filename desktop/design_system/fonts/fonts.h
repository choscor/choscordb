#pragma once

#include <QFont>
#include <QString>

namespace choscordb::design {

// Text styles on the bundled Geist family, named after Apple's macOS built-in
// styles. Sizes follow Geist's own scale: 14px body, 13px dense UI text and a
// 12px minimum, where Geist stays legible on low-DPI screens. The SQL editor keeps the user's saved
// font.
enum class TextStyle {
    LargeTitle, // 28/36 SemiBold
    Title1,     // 22/28 SemiBold
    Title2,     // 18/24 SemiBold
    Title3,     // 16/24 SemiBold
    Headline,   // 14/20 SemiBold: headings and dialog titles
    Body,       // 14/20 Regular: default UI text
    Callout,    // 13/18 Regular: table cells and headers, tree, list and menu items,
                // fields, tabs, secondary detail, field errors, sidebar sections
    Caption     // 12/16 Medium: badges, kbd
};

// SwiftUI's Font.Design: Monospaced sets a style in Geist Mono, as for
// read-only SQL previews and history SQL excerpts.
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

} // namespace choscordb::design
