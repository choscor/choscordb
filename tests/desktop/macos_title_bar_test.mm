#include "app/main_window.h"
#include "design_system/theme_manager.h"
#import <AppKit/AppKit.h>
#include <QApplication>
#include <QDockWidget>
#include <QTemporaryDir>
#include <QtTest>

static bool contentClearsTitleBar(choscordb::MainWindow& window, NSWindow* native) {
    if (native.styleMask & NSWindowStyleMaskFullSizeContentView)
        return false;
    const int contentTop = window.centralWidget()->mapTo(&window, QPoint(0, 0)).y();
    const auto* navigator = window.findChild<QDockWidget*>("navigator");
    const int navigatorTop = navigator ? navigator->mapTo(&window, QPoint(0, 0)).y() : -1;
    // AppKit reserves the native title bar outside Qt's content rectangle.
    return window.contentsMargins().top() == 0 && contentTop == 0 && navigatorTop == 0;
}

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
        if (native.titleVisibility != NSWindowTitleVisible ||
            ![native.title isEqualToString:@"ChoscorDB"] || !native.titlebarAppearsTransparent ||
            native.titlebarSeparatorStyle != NSTitlebarSeparatorStyleNone) {
            qCritical("Native title must be visible, transparent, and have no content separator");
            return 1;
        }
        if (!contentClearsTitleBar(window, native)) {
            qCritical("Qt content must begin directly below the native title bar");
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
    window.resize(980, 640);
    app.processEvents();
    if (!contentClearsTitleBar(window, native)) {
        qCritical("Resized content must begin directly below the native title bar");
        return 1;
    }
    window.showFullScreen();
    const bool enteredSafe = QTest::qWaitFor(
        [&] {
            return (native.styleMask & NSWindowStyleMaskFullScreen) &&
                   contentClearsTitleBar(window, native);
        },
        4000);
    // Qt applies the full-screen mask before AppKit finishes its transition.
    QTest::qWait(1200);
    const bool fullScreenSafe = enteredSafe && (native.styleMask & NSWindowStyleMaskFullScreen) &&
                                contentClearsTitleBar(window, native);
    window.showNormal();
    const bool exitedSafe = QTest::qWaitFor(
        [&] {
            return !(native.styleMask & NSWindowStyleMaskFullScreen) &&
                   contentClearsTitleBar(window, native);
        },
        4000);
    QTest::qWait(1200);
    const bool windowedSafe = exitedSafe && !(native.styleMask & NSWindowStyleMaskFullScreen) &&
                              contentClearsTitleBar(window, native);
    if (!fullScreenSafe || !windowedSafe) {
        qCritical("Qt content must remain below the native title bar through full-screen "
                  "transitions (fullSafe=%d, normalSafe=%d, mask=%lu, margin=%d)",
                  fullScreenSafe, windowedSafe, static_cast<unsigned long>(native.styleMask),
                  window.contentsMargins().top());
        return 1;
    }
    return 0;
}
