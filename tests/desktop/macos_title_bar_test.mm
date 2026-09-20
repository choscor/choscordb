#include "app/main_window.h"
#include "design_system/theme_manager.h"
#import <AppKit/AppKit.h>
#include <QApplication>
#include <QTemporaryDir>
#include <QtTest>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("profiles.sqlite"));
    window.show();
    app.processEvents();
    NSWindow* native = [reinterpret_cast<NSView*>(window.winId()) window];
    auto* theme = window.findChild<choscordb::design::ThemeManager*>();
    if (!native || !theme) {
        return 1;
    }
    for (const auto mode :
         {choscordb::design::ThemeMode::Dark, choscordb::design::ThemeMode::Light}) {
        theme->setMode(mode);
        app.processEvents();
        if (native.titleVisibility != NSWindowTitleHidden || !native.titlebarAppearsTransparent) {
            qCritical("Native title must be hidden and title bar transparent");
            return 1;
        }
        NSColor* color = [native.backgroundColor colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
        const QColor expected(mode == choscordb::design::ThemeMode::Dark ? "#171d20" : "#f6f7f8");
        if (qAbs(color.redComponent - expected.redF()) > 0.005 ||
            qAbs(color.greenComponent - expected.greenF()) > 0.005 ||
            qAbs(color.blueComponent - expected.blueF()) > 0.005 ||
            !(native.styleMask & NSWindowStyleMaskTitled) ||
            ![native standardWindowButton:NSWindowCloseButton]) {
            qCritical("Native title bar must follow theme and retain native window controls");
            return 1;
        }
    }
    return 0;
}
