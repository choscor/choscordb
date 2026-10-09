#include "design_system/theme.h"

#include <QApplication>
#include <QWidget>

namespace choscordb::design {
ResolvedTheme resolvedThemeForWidget(const QWidget& widget) {
    for (const QObject* scope = &widget; scope; scope = scope->parent()) {
        const auto theme = scope->property("designTheme");
        if (theme.canConvert<ResolvedTheme>())
            return theme.value<ResolvedTheme>();
    }
    if (qApp) {
        const auto theme = qApp->property("designTheme");
        if (theme.canConvert<ResolvedTheme>())
            return theme.value<ResolvedTheme>();
    }
    const auto appearance = widget.palette().color(QPalette::Window).lightnessF() < .5
                                ? ResolvedAppearance::Dark
                                : ResolvedAppearance::Light;
    return {appearance, resolveColors(appearance), false};
}

QPalette applicationPalette(const ResolvedTheme& theme) {
    const auto& colors = theme.colors;
    QPalette palette;
    palette.setColor(QPalette::Window, colors.bg);
    palette.setColor(QPalette::WindowText, colors.fg);
    palette.setColor(QPalette::Base, colors.surface);
    palette.setColor(QPalette::AlternateBase, colors.surfaceRaised);
    palette.setColor(QPalette::Text, colors.fg);
    palette.setColor(QPalette::Button, colors.surface);
    palette.setColor(QPalette::ButtonText, colors.fg);
    palette.setColor(QPalette::Highlight, colors.selection);
    palette.setColor(QPalette::HighlightedText, colors.fg);
    palette.setColor(QPalette::Link, colors.primary);
    palette.setColor(QPalette::Accent, colors.primary);
    palette.setColor(QPalette::Mid, colors.border);
    palette.setColor(QPalette::Dark, colors.ring);
    palette.setColor(QPalette::LinkVisited, colors.success);
    palette.setColor(QPalette::PlaceholderText, colors.fgMuted);
    palette.setColor(QPalette::BrightText, colors.danger);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, colors.fgDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, colors.fgDisabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, colors.fgDisabled);
    return palette;
}

} // namespace choscordb::design
