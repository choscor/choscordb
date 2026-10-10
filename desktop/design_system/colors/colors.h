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
// ChoscorDB color roles. Kebab-case names (`surface-raised`) are used in QSS
// and the gallery token catalog; see docs/design/design-system.md.
struct Colors final {
    QColor bg;
    QColor surface;
    QColor surfaceRaised;
    QColor sidebar;
    QColor fg;
    QColor fgMuted;
    QColor fgDisabled;
    QColor border;
    QColor primary;
    QColor primaryHover;
    QColor primaryPressed;
    QColor primaryFg;
    QColor selection;
    QColor ring;
    QColor success;
    QColor successSurface;
    QColor warning;
    QColor warningSurface;
    QColor danger;
    QColor dangerSurface;
    QColor backdrop;
    QColor switchTrack;
    QColor codeKeyword;
    QColor codeString;
    QColor codeNumber;
    QColor codeComment;

    friend bool operator==(const Colors&, const Colors&) = default;
};

[[nodiscard]] double contrastRatio(const QColor& foreground, const QColor& background);
[[nodiscard]] Colors resolveColors(ResolvedAppearance appearance);
[[nodiscard]] Colors resolveForcedContrastColors(const QPalette& palette);
// The switch thumb is computed rather than a role: `surface` in Light and
// white in Dark.
[[nodiscard]] QColor switchThumb(const Colors& colors, ResolvedAppearance appearance);
// Every color role with its kebab-case token name, in catalog order.
[[nodiscard]] QList<std::pair<QString, QColor>> colorTokens(const Colors& colors);

} // namespace choscordb::design

Q_DECLARE_METATYPE(choscordb::design::ThemeMode)
Q_DECLARE_METATYPE(choscordb::design::ResolvedAppearance)
