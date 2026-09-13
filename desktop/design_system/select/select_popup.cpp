#include "design_system/select/select_popup.h"
#include "design_system/theme.h"
#include <QAbstractItemView>
#include <QComboBox>

namespace choscordb::design::detail {
void prepareComboPopup(QComboBox& combo) {
    auto* view = combo.view();
    auto* popup = view->window();
    const auto theme = resolvedThemeForWidget(combo);
    const auto& colors = theme.colors;
    popup->setObjectName("designComboPopup");
    popup->setWindowFlag(Qt::NoDropShadowWindowHint);
    popup->setAttribute(Qt::WA_TranslucentBackground);
    popup->setAttribute(Qt::WA_MacShowFocusRect, false);
    popup->setPalette(applicationPalette(theme));
    // Qt deliberately excludes its private combo container from inherited QSS.
    // Give that real popup one explicit surface; keep its native list/delegate
    // so font pickers, keyboard selection, and custom models still work.
    popup->setStyleSheet(
        QStringLiteral(
            "QFrame#designComboPopup { background: %1; border: 1px solid %2; border-radius: 7px; }")
            .arg(colors.popover.name(), colors.border.name()));
    view->setAttribute(Qt::WA_MacShowFocusRect, false);
    view->setPalette(applicationPalette(theme));
    view->setStyleSheet(
        QStringLiteral(
            "QAbstractItemView { border: 0; background: %1; color: %2; outline: 0; padding: 4px; "
            "border-radius: 6px; selection-background-color: %3; selection-color: %2; }"
            "QAbstractItemView::item { min-height: 21px; padding: 4px 9px; border: 0; "
            "border-radius: 4px; }"
            "QAbstractItemView::item:selected { background: %3; color: %2; }"
            "QAbstractItemView::item:hover:!selected { background: %4; }"
            "QAbstractItemView::item:disabled { color: %5; }")
            .arg(colors.popover.name(), colors.foreground.name(), colors.accent.name(),
                 colors.muted.name(), colors.disabled.name()));
}

} // namespace choscordb::design::detail
