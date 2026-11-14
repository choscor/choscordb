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
    palette.setColor(QPalette::Window, colors.canvas);
    palette.setColor(QPalette::WindowText, colors.textDefault);
    palette.setColor(QPalette::Base, colors.base);
    palette.setColor(QPalette::AlternateBase, colors.elevated);
    palette.setColor(QPalette::Text, colors.textDefault);
    palette.setColor(QPalette::Button, colors.base);
    palette.setColor(QPalette::ButtonText, colors.textDefault);
    palette.setColor(QPalette::Highlight, colors.tint);
    palette.setColor(QPalette::HighlightedText, colors.textDefault);
    palette.setColor(QPalette::Link, colors.brand);
    palette.setColor(QPalette::Accent, colors.brand);
    palette.setColor(QPalette::Mid, colors.hairline);
    palette.setColor(QPalette::Dark, colors.brand);
    palette.setColor(QPalette::LinkVisited, colors.textSuccess);
    palette.setColor(QPalette::PlaceholderText, colors.textPlaceholder);
    palette.setColor(QPalette::BrightText, colors.textDanger);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, colors.textInactive);
    palette.setColor(QPalette::Disabled, QPalette::Text, colors.textInactive);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, colors.textInactive);
    return palette;
}

}
