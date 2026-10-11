#include "design_system/fonts/fonts.h"

#include <QDebug>
#include <QFontDatabase>

int qInitResources_resources();

namespace choscordb::design {
bool bundledFontsAvailable() {
    static const bool loaded = [] {
        ::qInitResources_resources();
        bool available = true;
        for (const auto* family : {"Geist", "GeistMono"}) {
            for (const auto* weight : {"Regular", "Medium", "SemiBold", "Bold"}) {
                const auto path = QStringLiteral(":/fonts/%1-%2.ttf")
                                      .arg(QLatin1String(family), QLatin1String(weight));
                if (QFontDatabase::addApplicationFont(path) < 0) {
                    qWarning() << "Could not load bundled font:" << path;
                    available = false;
                }
            }
        }
        return available;
    }();
    return loaded;
}

namespace {
QString familyFor(FontDesign design) {
    if (bundledFontsAvailable()) {
        return design == FontDesign::Monospaced ? QStringLiteral("Geist Mono")
                                                : QStringLiteral("Geist");
    }
    return QFontDatabase::systemFont(design == FontDesign::Monospaced ? QFontDatabase::FixedFont
                                                                      : QFontDatabase::GeneralFont)
        .family();
}
} // namespace

TypographySpec typographySpec(TextStyle style, FontDesign design) {
    const auto family = familyFor(design);
    switch (style) {
    case TextStyle::LargeTitle:
        return {family, 28, 36, QFont::DemiBold};
    case TextStyle::Title1:
        return {family, 22, 28, QFont::DemiBold};
    case TextStyle::Title2:
        return {family, 18, 24, QFont::DemiBold};
    case TextStyle::Title3:
        return {family, 16, 24, QFont::DemiBold};
    case TextStyle::Headline:
        return {family, 14, 20, QFont::DemiBold};
    case TextStyle::Body:
        return {family, 14, 20, QFont::Normal};
    case TextStyle::Callout:
        return {family, 13, 18, QFont::Normal};
    case TextStyle::Caption:
        return {family, 12, 16, QFont::Medium};
    }
    return {};
}

QFont resolveTypography(TextStyle style, FontDesign design) {
    const auto spec = typographySpec(style, design);
    QFont font(spec.family);
    font.setPixelSize(spec.pixelSize);
    font.setWeight(spec.weight);
    // Snapping only the vertical axis keeps Geist's 12px x-height crisp on
    // Windows and Linux without distorting its spacing. macOS ignores it.
    font.setHintingPreference(QFont::PreferVerticalHinting);
    return font;
}

} // namespace choscordb::design
