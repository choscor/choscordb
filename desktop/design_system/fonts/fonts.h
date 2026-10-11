#pragma once

#include <QFont>
#include <QString>

namespace choscordb::design {

// The macOS built-in text styles from Apple's Human Interface Guidelines, on
// the platform UI font (SF Pro on macOS). The SQL editor keeps the user's
// saved font.
enum class TextStyle {
    LargeTitle,  // 26/32 Regular
    Title1,      // 22/26 Regular
    Title2,      // 17/22 Regular
    Title3,      // 15/20 Regular
    Headline,    // 13/16 Bold: headings and dialog titles
    Body,        // 13/16 Regular: default UI text
    Callout,     // 12/15 Regular: table cells, headers, tree, list and menu items, fields
    Subheadline, // 11/14 Regular: secondary detail, field errors, tab labels, sidebar sections
    Footnote,    // 10/13 Regular
    Caption1,    // 10/13 Regular
    Caption2     // 10/13 Medium: badges, kbd
};

// SwiftUI's Font.Design: Monospaced sets a style in the platform fixed-pitch
// font, as for read-only SQL previews and history SQL excerpts.
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
