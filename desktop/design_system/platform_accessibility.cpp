#include "design_system/platform_accessibility.h"

#include "design_system/theme_manager.h"

#include <QApplication>
#include <QStyle>
#include <QTimer>

#if defined(Q_OS_MACOS)
#include <objc/message.h>
#include <objc/runtime.h>
#elif defined(Q_OS_WIN)
#define NOMINMAX
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#endif

namespace choscordb::design {
namespace {

#if defined(Q_OS_MACOS)
bool workspaceBoolean(const char* selectorName) {
    using SendObject = id (*)(id, SEL);
    using SendBoolean = BOOL (*)(id, SEL);
    auto* workspaceClass = objc_getClass("NSWorkspace");
    if (workspaceClass == nullptr) {
        return false;
    }
    const auto workspace = reinterpret_cast<SendObject>(objc_msgSend)(
        reinterpret_cast<id>(workspaceClass), sel_registerName("sharedWorkspace"));
    const auto selector = sel_registerName(selectorName);
    if (workspace == nullptr || !class_respondsToSelector(object_getClass(workspace), selector)) {
        return false;
    }
    return reinterpret_cast<SendBoolean>(objc_msgSend)(workspace, selector) != NO;
}
#endif

} // namespace

PlatformAccessibilityPreferences readPlatformAccessibilityPreferences() {
#if defined(Q_OS_MACOS)
    return {
        .forcedContrast = workspaceBoolean("accessibilityDisplayShouldIncreaseContrast"),
        .reducedMotion = workspaceBoolean("accessibilityDisplayShouldReduceMotion"),
    };
#elif defined(Q_OS_WIN)
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(HIGHCONTRASTW);
    BOOL animationsEnabled = TRUE;
    const bool contrastAvailable =
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE;
    const bool animationAvailable =
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animationsEnabled, 0) != FALSE;
    return {
        .forcedContrast = contrastAvailable && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0,
        .reducedMotion = animationAvailable && animationsEnabled == FALSE,
    };
#else
    // Linux portal reads are asynchronous and owned by PlatformAccessibilityMonitor.
    return {};
#endif
}

PlatformAccessibilityMonitor::PlatformAccessibilityMonitor(ThemeManager* theme, QObject* parent)
    : QObject(parent), theme_(theme), current_(readPlatformAccessibilityPreferences()),
      systemPalette_(qApp->style()->standardPalette()) {
    theme_->setSystemPalette(systemPalette_);
    theme_->setForcedContrast(current_.forcedContrast);
    theme_->setReducedMotion(current_.reducedMotion);
    auto* timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, &PlatformAccessibilityMonitor::refresh);
    timer->start();
#if defined(Q_OS_LINUX)
    refresh();
#endif
}

void PlatformAccessibilityMonitor::refresh() {
#if defined(Q_OS_LINUX)
    if (linuxReadsPending_ != 0) {
        return;
    }
    linuxReadsPending_ = 2;
    requestLinuxPreference(QStringLiteral("contrast"),
                           &PlatformAccessibilityPreferences::forcedContrast);
    requestLinuxPreference(QStringLiteral("reduced-motion"),
                           &PlatformAccessibilityPreferences::reducedMotion);
#else
    const auto updated = readPlatformAccessibilityPreferences();
    const auto updatedPalette = qApp->style()->standardPalette();
    if (updated == current_ && updatedPalette == systemPalette_) {
        return;
    }
    current_ = updated;
    systemPalette_ = updatedPalette;
    theme_->setSystemPalette(systemPalette_);
    theme_->setForcedContrast(current_.forcedContrast);
    theme_->setReducedMotion(current_.reducedMotion);
#endif
}

#if defined(Q_OS_LINUX)
void PlatformAccessibilityMonitor::requestLinuxPreference(
    const QString& key, bool PlatformAccessibilityPreferences::* field) {
    QDBusInterface settings(QStringLiteral("org.freedesktop.portal.Desktop"),
                            QStringLiteral("/org/freedesktop/portal/desktop"),
                            QStringLiteral("org.freedesktop.portal.Settings"),
                            QDBusConnection::sessionBus());
    auto* watcher = new QDBusPendingCallWatcher(
        settings.asyncCall(QStringLiteral("ReadOne"), QStringLiteral("org.freedesktop.appearance"),
                           key),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, field](QDBusPendingCallWatcher*) {
                const QDBusPendingReply<QDBusVariant> reply = *watcher;
                watcher->deleteLater();
                --linuxReadsPending_;
                if (reply.isError()) {
                    return;
                }
                auto updated = current_;
                updated.*field = reply.value().variant().toUInt() == 1;
                const auto updatedPalette = qApp->style()->standardPalette();
                if (updated == current_ && updatedPalette == systemPalette_) {
                    return;
                }
                current_ = updated;
                systemPalette_ = updatedPalette;
                theme_->setSystemPalette(systemPalette_);
                theme_->setForcedContrast(current_.forcedContrast);
                theme_->setReducedMotion(current_.reducedMotion);
            });
}
#endif

} // namespace choscordb::design
