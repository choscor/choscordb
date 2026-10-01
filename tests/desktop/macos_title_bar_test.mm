#include "app/main_window.h"
#include "design_system/theme_manager.h"
#import <AppKit/AppKit.h>
#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <functional>

static bool contentClearsTitleBar(choscordb::MainWindow& window, NSWindow* native) {
    if (native.styleMask & NSWindowStyleMaskFullSizeContentView)
        return false;
    const int contentTop = window.centralWidget()->mapTo(&window, QPoint(0, 0)).y();
    const auto* navigator = window.findChild<QDockWidget*>("navigator");
    const int navigatorTop = navigator ? navigator->mapTo(&window, QPoint(0, 0)).y() : -1;
    // AppKit reserves the native title bar outside Qt's content rectangle.
    return window.contentsMargins().top() == 0 && contentTop == 0 && navigatorTop == 0;
}

static bool waitForNativeState(const std::function<bool()>& ready) {
    // A manually pumped processEvents() does not enter Cocoa's NSApp run loop.
    // Full-screen completion needs that native loop, just like the running app.
    QEventLoop loop;
    QTimer poll;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (ready())
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(10);
    deadline.start(4000);
    loop.exec();
    return ready();
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("profiles.sqlite"));
    window.show();
    window.activateWindow();
    window.raise();
    if (!QTest::qWaitForWindowActive(&window, 4000)) {
        qCritical("Native title bar test window must be active before transitions");
        return 1;
    }
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
    bool entered = false;
    auto* enteredState = &entered;
    bool exited = false;
    auto* exitedState = &exited;
    auto* center = [NSNotificationCenter defaultCenter];
    id enterObserver = [center addObserverForName:NSWindowDidEnterFullScreenNotification
                                           object:native
                                            queue:nil
                                       usingBlock:^(NSNotification*) {
                                         *enteredState = true;
                                       }];
    id exitObserver = [center addObserverForName:NSWindowDidExitFullScreenNotification
                                          object:native
                                           queue:nil
                                      usingBlock:^(NSNotification*) {
                                        *exitedState = true;
                                      }];
    window.showFullScreen();
    // Qt sets the style mask before AppKit completes the transition. Wait for
    // the native completion notification before asking it to exit full screen.
    const bool fullScreenSafe = waitForNativeState([&] {
        return entered && (native.styleMask & NSWindowStyleMaskFullScreen) &&
               contentClearsTitleBar(window, native);
    });
    bool windowedSafe = false;
    if (fullScreenSafe) {
        window.showNormal();
        windowedSafe = waitForNativeState([&] {
            return exited && !(native.styleMask & NSWindowStyleMaskFullScreen) &&
                   contentClearsTitleBar(window, native);
        });
    }
    [center removeObserver:enterObserver];
    [center removeObserver:exitObserver];
    if (!fullScreenSafe || !windowedSafe) {
        qCritical(
            "Qt content must remain below the native title bar through full-screen "
            "transitions (fullSafe=%d, normalSafe=%d, mask=%lu, margin=%d, entered=%d, exited=%d)",
            fullScreenSafe, windowedSafe, static_cast<unsigned long>(native.styleMask),
            window.contentsMargins().top(), entered, exited);
        return 1;
    }
    return 0;
}
