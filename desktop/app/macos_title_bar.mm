#include "app/main_window.h"
#include "design_system/theme_manager.h"
#import <AppKit/AppKit.h>
#include <QApplication>
#include <QShowEvent>

namespace choscordb {

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    updateNativeTitleBar();
}

void MainWindow::updateNativeTitleBar() {
    // Offscreen Qt windows do not expose an NSView. Do not create a native
    // window early while the workspace and its saved appearance are loading.
    if (QApplication::platformName() != QStringLiteral("cocoa") ||
        !testAttribute(Qt::WA_WState_Created)) {
        return;
    }
    NSWindow* window = [reinterpret_cast<NSView*>(winId()) window];
    if (!window) {
        return;
    }
    const auto color = theme_->resolvedTheme().colors.background;
    window.titleVisibility = NSWindowTitleHidden;
    window.titlebarAppearsTransparent = YES;
    window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
    window.backgroundColor = [NSColor colorWithSRGBRed:color.redF()
                                                 green:color.greenF()
                                                  blue:color.blueF()
                                                 alpha:1.0];
    window.appearance = [NSAppearance
        appearanceNamed:theme_->resolvedTheme().appearance == design::ResolvedAppearance::Dark
                            ? NSAppearanceNameDarkAqua
                            : NSAppearanceNameAqua];
}

} // namespace choscordb
