#include "design_system/fonts/fonts.h"

#include <QDebug>
#include <QFontDatabase>

int qInitResources_resources();

namespace choscordb::design {
bool bundledFontsAvailable() {
    static const bool loaded = [] {
        ::qInitResources_resources();
        bool available = true;
        for (const auto* weight : {"Regular", "Medium", "SemiBold", "Bold"}) {
            const auto path = QStringLiteral(":/fonts/Geist-%1.ttf").arg(QLatin1String(weight));
            if (QFontDatabase::addApplicationFont(path) < 0) {
                qWarning() << "Could not load bundled font:" << path;
                available = false;
            }
        }
        return available;
    }();
    return loaded;
}

TypographySpec typographySpec(TextStyle style, FontDesign design) {
    const auto family = QFontDatabase::systemFont(design == FontDesign::Monospaced
                                                      ? QFontDatabase::FixedFont
                                                      : QFontDatabase::GeneralFont)
                            .family();
    switch (style) {
    case TextStyle::LargeTitle:
        return {family, 26, 32, QFont::Normal};
    case TextStyle::Title1:
        return {family, 22, 26, QFont::Normal};
    case TextStyle::Title2:
        return {family, 17, 22, QFont::Normal};
    case TextStyle::Title3:
        return {family, 15, 20, QFont::Normal};
    case TextStyle::Headline:
        return {family, 13, 16, QFont::Bold};
    case TextStyle::Body:
        return {family, 13, 16, QFont::Normal};
    case TextStyle::Callout:
        return {family, 12, 15, QFont::Normal};
    case TextStyle::Subheadline:
        return {family, 11, 14, QFont::Normal};
    case TextStyle::Footnote:
    case TextStyle::Caption1:
        return {family, 10, 13, QFont::Normal};
    case TextStyle::Caption2:
        return {family, 10, 13, QFont::Medium};
    }
    return {};
}

QFont resolveTypography(TextStyle style, FontDesign design) {
    // Keep previously selectable bundled families available to saved SQL font
    // preferences, while all UI styles continue to request the platform font.
    (void)bundledFontsAvailable();
    // The platform font applies its own size-specific tracking.
    const auto spec = typographySpec(style, design);
    auto font = QFontDatabase::systemFont(design == FontDesign::Monospaced
                                              ? QFontDatabase::FixedFont
                                              : QFontDatabase::GeneralFont);
    font.setPixelSize(spec.pixelSize);
    font.setWeight(spec.weight);
    return font;
}

} // namespace choscordb::design
