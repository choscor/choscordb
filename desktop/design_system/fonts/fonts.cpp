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
    switch (role) {
    case TypographyRole::SectionCaption:
        return {family, 10, 14, QFont::DemiBold};
    case TypographyRole::Metadata:
        return {QFontDatabase::systemFont(QFontDatabase::FixedFont).family(), 12, 17,
                QFont::Normal};
    case TypographyRole::Small:
        return {family, 11, 16, QFont::Normal};
    case TypographyRole::NavigationDetail:
        return {family, 10, 16, QFont::Normal};
    case TypographyRole::Heading:
        return {family, 14, 20, QFont::DemiBold};
    case TypographyRole::Base:
        return {family, 13, 22, QFont::Normal};
    case TypographyRole::DialogTitle:
        return {family, 14, 20, QFont::DemiBold};
    case TypographyRole::Monospace:
        return {QFontDatabase::systemFont(QFontDatabase::FixedFont).family(), 13, 24,
                QFont::Normal};
    case TypographyRole::Ui:
        return {family, 13, 18, QFont::Normal};
    case TypographyRole::Field:
        return {family, 12, 17, QFont::Medium};
    }
    return {};
}

QFont resolveTypography(TypographyRole role) {
    // Keep previously selectable bundled families available to saved SQL font
    // preferences, while all UI roles continue to request the platform font.
    (void)bundledFontsAvailable();
    // SQL editors apply their own saved font preferences; preserve that seam.
    if (role == TypographyRole::Monospace || role == TypographyRole::Metadata) {
        auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPixelSize(typographySpec(role).pixelSize);
        return font;
    }
    const auto spec = typographySpec(role);
    auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPixelSize(spec.pixelSize);
    font.setWeight(spec.weight);
    font.setLetterSpacing(QFont::AbsoluteSpacing, role == TypographyRole::SectionCaption ? 1.3
                                                  : role == TypographyRole::Field        ? 0
                                                                                         : -0.12);
    return font;
}

} // namespace choscordb::design
