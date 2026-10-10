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

TypographySpec typographySpec(TypographyRole role) {
    const auto family = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    const auto fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    switch (role) {
    case TypographyRole::Caption:
        return {family, 10, 14, QFont::DemiBold};
    case TypographyRole::Small:
        return {family, 11, 16, QFont::Normal};
    case TypographyRole::Dense:
        return {family, 12, 16, QFont::Normal};
    case TypographyRole::Body:
        return {family, 13, 18, QFont::Normal};
    case TypographyRole::Title:
        return {family, 14, 20, QFont::DemiBold};
    case TypographyRole::Mono:
        return {fixed, 13, 20, QFont::Normal};
    case TypographyRole::Metadata:
        return {fixed, 12, 16, QFont::Normal};
    }
    return {};
}

QFont resolveTypography(TypographyRole role) {
    // Keep previously selectable bundled families available to saved SQL font
    // preferences, while all UI roles continue to request the platform font.
    (void)bundledFontsAvailable();
    // SQL editors apply their own saved font preferences; preserve that seam.
    if (role == TypographyRole::Mono || role == TypographyRole::Metadata) {
        auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPixelSize(typographySpec(role).pixelSize);
        return font;
    }
    const auto spec = typographySpec(role);
    auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPixelSize(spec.pixelSize);
    font.setWeight(spec.weight);
    font.setLetterSpacing(QFont::AbsoluteSpacing,
                          role == TypographyRole::Caption                                 ? 1.3
                          : role == TypographyRole::Body || role == TypographyRole::Title ? -0.12
                                                                                          : 0);
    return font;
}

} // namespace choscordb::design
