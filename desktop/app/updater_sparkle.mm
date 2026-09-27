// Qt 6.8 references the ARM yield intrinsic in Objective-C++ headers.
#include <arm_acle.h>

#include "app/main_window.h"
#include "app/updater.h"
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#import <Sparkle/Sparkle.h>

@interface ChoscorUpdaterDelegate : NSObject <SPUUpdaterDelegate> {
  @public
    QPointer<choscordb::MainWindow> window;
    QPointer<QAction> checkAction;
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
            checkAction->setEnabled(self.installHandler != nil ||
                                    self.controller.updater.canCheckForUpdates);
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
    if (checkAction)
        checkAction->setEnabled(true);
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
        if (checkAction)
            checkAction->setEnabled(self.controller.updater.canCheckForUpdates);
        if (handler)
            handler();
    });
}
- (void)dealloc {
    [self.controller.updater removeObserver:self forKeyPath:@"canCheckForUpdates"];
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
        check->setIcon(window.style()->standardIcon(QStyle::SP_BrowserReload));
        check->setIconVisibleInMenu(true);
        check->setEnabled(false);
        delegate_ = [[ChoscorUpdaterDelegate alloc] init];
        delegate_->window = &window;
        delegate_->checkAction = check;
        delegate_.controller =
            [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO
                                                          updaterDelegate:delegate_
                                                       userDriverDelegate:nil];
        [delegate_.controller.updater
            addObserver:delegate_
             forKeyPath:@"canCheckForUpdates"
                options:(NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew)
                context:nullptr];
        // A cancelled close leaves Sparkle's postponed install pending. The remaining
        // update action must let the user retry after resolving unsaved work.
        connect(check, &QAction::triggered, this, [this] {
            if (delegate_.installHandler)
                [delegate_ retryInstall];
            else
                [delegate_.controller checkForUpdates:nil];
        });
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
