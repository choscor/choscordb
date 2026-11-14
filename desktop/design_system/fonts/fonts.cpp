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
            const auto path = QStringLiteral(":/fonts/Inter-%1.ttf").arg(QLatin1String(weight));
            if (QFontDatabase::addApplicationFont(path) < 0) {
                qWarning() << "Could not load bundled font:" << path;
                available = false;
            }
        }
        return available;
    }();
    return loaded;
}

namespace {
QString monospaceFamily() {
    static const QString family = [] {
        for (const auto* candidate : {"SF Mono", "SFMono-Regular", "Menlo", "Monaco", "Consolas",
                                      "Liberation Mono", "Courier New"}) {
            if (QFontDatabase::hasFamily(QLatin1String(candidate)))
                return QString::fromLatin1(candidate);
        }
        return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    }();
    return family;
}

QString familyFor(FontDesign design) {
    if (design == FontDesign::Monospaced)
        return monospaceFamily();
    if (bundledFontsAvailable())
        return QStringLiteral("Inter");
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
}
}

TypographySpec typographySpec(TextStyle style, FontDesign design) {
    const auto family = familyFor(design);
    switch (style) {
    case TextStyle::Headline:
        return {family, 14, 21, QFont::DemiBold};
    case TextStyle::Body:
        return {family, 14, 21, QFont::Normal};
    case TextStyle::Callout:
        return {family, 13, 15, QFont::Normal};
    case TextStyle::Caption:
        return {family, 12, 16, QFont::Medium};
    }
    return {};
}

QFont resolveTypography(TextStyle style, FontDesign design) {
    constexpr qreal kumoTrackingEm = -0.01;
    const auto spec = typographySpec(style, design);
    QFont font(spec.family);
    font.setPixelSize(spec.pixelSize);
    font.setWeight(spec.weight);
    font.setHintingPreference(QFont::PreferVerticalHinting);
    if (design == FontDesign::Default) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, kumoTrackingEm * spec.pixelSize);
        for (const auto tag :
             {QFont::Tag("cv02"), QFont::Tag("cv03"), QFont::Tag("cv04"), QFont::Tag("calt")})
            font.setFeature(tag, 1);
    }
    return font;
}

QFont resolveSectionLabelTypography() {
    constexpr qreal sectionTrackingEm = 0.04;
    auto font = resolveTypography(TextStyle::Caption);
    font.setWeight(QFont::DemiBold);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, sectionTrackingEm * font.pixelSize());
    return font;
}

}
