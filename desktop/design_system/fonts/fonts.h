#pragma once

#include <QFont>
#include <QString>

namespace choscordb::design {

// Seven roles on the platform UI and fixed fonts. The SQL editor keeps the
// user's saved font; Mono is for read-only SQL previews.
enum class TypographyRole {
    Caption, // 10/14 600 +1.3px: section captions, badges, kbd
    Small,   // 11/16: secondary detail, field errors, tab labels
    Dense,   // 12/16: table cells, headers, tree, list and menu items, fields
    Body,    // 13/18 -0.12px: default UI text
    Title,   // 14/20 600 -0.12px: headings and dialog titles
    Mono,    // 13/20 fixed: SQL previews
    Metadata // 12/16 fixed: SQL snippets in history rows
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
