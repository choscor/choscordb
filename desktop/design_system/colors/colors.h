#pragma once

#include <QColor>
#include <QList>
#include <QMetaType>
#include <QPalette>
#include <QString>
#include <utility>

namespace choscordb::design {

enum class ThemeMode { System, Light, Dark };
enum class ResolvedAppearance { Light, Dark };
struct Colors final {
    QColor canvas;
    QColor elevated;
    QColor recessed;
    QColor overlay;
    QColor base;
    QColor control;
    QColor tint;
    QColor fill;
    QColor contrast;
    QColor badgeInverted;
    QColor brand;
    QColor brandHover;
    QColor line;
    QColor hairline;
    QColor focus;
    QColor shadowEdge;
    QColor shadowDrop;
    QColor arrowEdge;
    QColor arrowStroke;
    QColor danger;
    QColor successTint;
    QColor warningTint;
    QColor dangerTint;
    QColor textDefault;
    QColor textStrong;
    QColor textInverse;
    QColor textSubtle;
    QColor textInactive;
    QColor textPlaceholder;
    QColor textSuccess;
    QColor textDanger;
    QColor textWarning;
    QColor textBadgeInverted;
    QColor textBadgeNeutralSubtle;
    QColor white;
    QColor codeKeyword;
    QColor codeString;
    QColor codeNumber;
    QColor codeComment;
    QColor codeKey;

    friend bool operator==(const Colors&, const Colors&) = default;
};

enum class Emphasis { Brand, Danger };

struct EmphasisFill final {
    QColor ring;
    QColor background;
    QColor gradientStart;
    QColor gradientEnd;
};

[[nodiscard]] double contrastRatio(const QColor& foreground, const QColor& background);
[[nodiscard]] QColor composite(const QColor& top, const QColor& bottom);
[[nodiscard]] Colors resolveColors(ResolvedAppearance appearance);
[[nodiscard]] Colors resolveForcedContrastColors(const QPalette& palette);
[[nodiscard]] QColor withOpacity(const QColor& token, double opacity);
enum class Scrim { Dialog, CommandPalette };
[[nodiscard]] QColor dialogBackdrop(const Colors& colors, Scrim scrim);
struct BannerColors final {
    QColor fill;
    QColor text;
};
[[nodiscard]] BannerColors secondaryBanner(const Colors& colors);
struct SwitchColors final {
    QColor track;
    QColor ring;
    QColor thumb;
};
[[nodiscard]] SwitchColors switchColors(const Colors& colors, ResolvedAppearance appearance,
                                        bool forcedContrast, bool checked);
[[nodiscard]] EmphasisFill emphasisFill(const Colors& colors, ResolvedAppearance appearance,
                                        bool forcedContrast, Emphasis emphasis);
[[nodiscard]] QList<std::pair<QString, QColor>> colorTokens(const Colors& colors);

}

Q_DECLARE_METATYPE(choscordb::design::ThemeMode)
Q_DECLARE_METATYPE(choscordb::design::ResolvedAppearance)
