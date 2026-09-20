// Qt 6.8 references the ARM yield intrinsic in Objective-C++ headers.
#include <arm_acle.h>

#include "app/main_window.h"
#include "app/updater.h"
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#import <Sparkle/Sparkle.h>

@interface ChoscorUpdaterDelegate : NSObject <SPUUpdaterDelegate> {
  @public
    QPointer<choscordb::MainWindow> window;
    QPointer<QAction> checkAction;
    QPointer<QAction> automaticAction;
    QPointer<QAction> installAction;
}
- (void)retryInstall;
@property(nonatomic, strong) SPUStandardUpdaterController* controller;
@property(nonatomic, copy) void (^installHandler)(void);
@end

@implementation ChoscorUpdaterDelegate
- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id>*)change
                       context:(void*)context {
    if ([keyPath isEqualToString:@"canCheckForUpdates"]) {
        if (checkAction)
            checkAction->setEnabled(self.controller.updater.canCheckForUpdates);
    } else if ([keyPath isEqualToString:@"automaticallyChecksForUpdates"]) {
        if (automaticAction)
            automaticAction->setChecked(self.controller.updater.automaticallyChecksForUpdates);
    } else {
        [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
    }
}
- (BOOL)updater:(SPUUpdater*)updater
    shouldPostponeRelaunchForUpdate:(SUAppcastItem*)item
                 untilInvokingBlock:(void (^)(void))installHandler {
    (void)updater;
    (void)item;
    self.installHandler = installHandler;
    if (installAction)
        installAction->setEnabled(true);
    // Return to Sparkle before starting Qt's asynchronous persistence/shutdown path.
    QTimer::singleShot(0, window, [self] { [self retryInstall]; });
    return YES;
}
- (void)retryInstall {
    if (!window || !self.installHandler)
        return;
    window->requestUpdateRestart([self] {
        auto handler = self.installHandler;
        self.installHandler = nil;
        if (installAction)
            installAction->setEnabled(false);
        if (handler)
            handler();
    });
}
- (void)dealloc {
    [self.controller.updater removeObserver:self forKeyPath:@"canCheckForUpdates"];
    [self.controller.updater removeObserver:self forKeyPath:@"automaticallyChecksForUpdates"];
}
@end

namespace choscordb {
namespace {
class NativeUpdater final : public QObject {
  public:
    explicit NativeUpdater(MainWindow& window) : QObject(&window) {
        auto* menu = window.menuBar()->addMenu(tr("Updates"));
        auto* check = menu->addAction(tr("Check for Updates…"));
        check->setObjectName("checkForUpdates");
        check->setMenuRole(QAction::ApplicationSpecificRole);
        check->setEnabled(false);
        auto* automatic = menu->addAction(tr("Automatically Check for Updates"));
        automatic->setObjectName("automaticUpdateChecks");
        automatic->setMenuRole(QAction::ApplicationSpecificRole);
        automatic->setCheckable(true);
        auto* install = menu->addAction(tr("Install Downloaded Update…"));
        install->setObjectName("installDownloadedUpdate");
        install->setMenuRole(QAction::ApplicationSpecificRole);
        install->setEnabled(false);
        delegate_ = [[ChoscorUpdaterDelegate alloc] init];
        delegate_->window = &window;
        delegate_->checkAction = check;
        delegate_->automaticAction = automatic;
        delegate_->installAction = install;
        delegate_.controller =
            [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO
                                                          updaterDelegate:delegate_
                                                       userDriverDelegate:nil];
        for (NSString* key in @[ @"canCheckForUpdates", @"automaticallyChecksForUpdates" ])
            [delegate_.controller.updater
                addObserver:delegate_
                 forKeyPath:key
                    options:(NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew)
                    context:nullptr];
        connect(check, &QAction::triggered, this,
                [this] { [delegate_.controller checkForUpdates:nil]; });
        connect(automatic, &QAction::triggered, this, [this](bool enabled) {
            delegate_.controller.updater.automaticallyChecksForUpdates = enabled;
        });
        connect(install, &QAction::triggered, this, [this] { [delegate_ retryInstall]; });
        // Sparkle owns consent and persisted NSUserDefaults. With no plist override,
        // its standard permission prompt appears before scheduled checks are enabled.
        [delegate_.controller startUpdater];
    }

  private:
    ChoscorUpdaterDelegate* delegate_;
};
} // namespace
void installNativeUpdater(MainWindow& window, bool isolated) {
    if (!isolated && !QStandardPaths::isTestModeEnabled() &&
        qEnvironmentVariable("QT_QPA_PLATFORM") != "offscreen")
        new NativeUpdater(window);
}
} // namespace choscordb
