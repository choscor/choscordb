#include "app/appearance_controller.h"
#include "design_system/theme_manager.h"
#include <QDockWidget>
#include <QEvent>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMouseEvent>
#include <QScreen>
#include <QSplitter>
#include <QTimer>
#include <algorithm>
#include <atomic>

namespace choscordb {
namespace {
AppearanceLayout defaultLayout() {
    AppearanceLayout value;
    const design::DesignMetrics metrics;
    value.navigatorWidth = metrics.initialNavigatorWidth;
    value.editorResultsSplit = metrics.initialEditorResultsSplit;
    return value;
}
design::ThemeMode themeMode(const QString& value) {
    if (value == "dark")
        return design::ThemeMode::Dark;
    if (value == "light")
        return design::ThemeMode::Light;
    return design::ThemeMode::System;
}
} // namespace

quint64 AppearanceController::nextToken() {
    static std::atomic<quint64> token{quint64(1) << 55};
    return token.fetch_add(1);
}
AppearanceController::AppearanceController(design::ThemeManager* theme, EngineAdapter* adapter,
                                           QMainWindow* window, QDockWidget* navigator,
                                           QSplitter* workspace, QWidget* history)
    : QObject(window), theme_(theme), adapter_(adapter), window_(window), navigator_(navigator),
      workspace_(workspace), saveTimer_(new QTimer(this)) {
    Q_UNUSED(history);
    connect(theme_, &design::ThemeManager::accessibilityPolicyChanged, this,
            &AppearanceController::accessibilityPolicyChanged);
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(300);
    connect(saveTimer_, &QTimer::timeout, this, [this] { submitSave(Request::AutomaticSave); });
    connect(workspace_, &QSplitter::splitterMoved, this, [this] { scheduleSave(); });
    connect(navigator_, &QDockWidget::visibilityChanged, this, [this] { scheduleSave(); });
    navigator_->installEventFilter(this);
    connect(theme_, &design::ThemeManager::themeChanged, this, [this] { scheduleSave(); });
    connect(theme_, &design::ThemeManager::metricsChanged, this, [this] { scheduleSave(); });
    window_->installEventFilter(this);
    connect(adapter_, &EngineAdapter::appearanceLayoutReady, this,
            [this](quint64 token, bool hasSaved, const AppearanceLayout& value) {
                if (token_ != token)
                    return;
                const auto request = request_;
                token_ = 0;
                request_ = Request::None;
                if (request == Request::Load || request == Request::Reset) {
                    defaultSidebar_ = !hasSaved;
                    persisted_ = hasSaved ? value : defaultLayout();
                    apply(persisted_, true);
                    loaded_ = true;
                    automaticAllowed_ = true;
                    persistentWarning_.clear();
                    emit readyChanged(true);
                    emit resolvedChoicesChanged(persisted_);
                    emit warningChanged({});
                } else if (request == Request::Save || request == Request::AutomaticSave) {
                    if (hasSaved) {
                        persisted_ = value;
                        automaticAllowed_ = true;
                        persistentWarning_.clear();
                        emit warningChanged({});
                    }
                    if (request == Request::Save) {
                        const bool resetLayout = resetPreview_;
                        if (resetLayout)
                            defaultSidebar_ = true;
                        resetPreview_ = false;
                        previewing_ = false;
                        apply(persisted_, resetLayout);
                    }
                    if (request == Request::Save)
                        emit saveFinished(true, tr("Appearance saved."));
                    if (flushRequested_) {
                        if (dirty_)
                            submitSave(Request::AutomaticSave);
                        else {
                            flushRequested_ = false;
                            emit flushReady();
                        }
                    } else if (dirty_)
                        scheduleSave();
                }
                if ((request == Request::Load || request == Request::Reset) && flushRequested_) {
                    flushRequested_ = false;
                    emit flushReady();
                }
            });
    connect(adapter_, &EngineAdapter::recoveryFailed, this,
            [this](quint64 token, const QString& error) {
                if (token_ != token)
                    return;
                const auto request = request_;
                token_ = 0;
                request_ = Request::None;
                if (request == Request::Load) {
                    loaded_ = true;
                    automaticAllowed_ = false;
                    emit readyChanged(true);
                    persistentWarning_ =
                        tr("Appearance could not be loaded: %1. Retry or reset in Preferences.")
                            .arg(error);
                    emit warningChanged(persistentWarning_);
                } else if (request == Request::Save)
                    emit saveFinished(false, error);
                else if (request == Request::Reset)
                    emit warningChanged(tr("Appearance reset failed: %1").arg(error));
                else if (request == Request::AutomaticSave) {
                    dirty_ = true;
                    emit warningChanged(tr("Layout could not be saved: %1").arg(error));
                }
                if (flushRequested_) {
                    flushRequested_ = false;
                    emit flushFailed(error);
                }
            });
    token_ = nextToken();
    request_ = Request::Load;
    adapter_->getAppearanceLayout(token_);
}

bool AppearanceController::forcedContrast() const {
    return theme_->forcedContrast();
}

bool AppearanceController::reducedMotion() const {
    return theme_->reducedMotion();
}

AppearanceLayout AppearanceController::current() const {
    auto result = persisted_;
    result.theme = theme_->mode() == design::ThemeMode::Dark    ? QStringLiteral("dark")
                   : theme_->mode() == design::ThemeMode::Light ? QStringLiteral("light")
                                                                : QStringLiteral("system");
    // Obsolete choices remain byte-compatible in storage, but no longer
    // participate in rendering or theme-only edits.
    result.navigatorWidth = static_cast<quint32>(
        std::max(navigator_->width(), theme_->metrics().minimumNavigatorWidth));
    const auto sizes = workspace_->sizes();
    const int total = sizes.size() >= 2 ? sizes[0] + sizes[1] : 0;
    if (total > 0)
        result.editorResultsSplit =
            static_cast<quint16>(std::clamp((sizes[0] * 1000 + total / 2) / total, 100, 900));
    // Dock placement is obsolete; retain compatible stored geometry and split.
    result.navigatorVisible = true;
    result.historyVisible = false;
    const auto geometry = window_->normalGeometry();
    result.x = geometry.x();
    result.y = geometry.y();
    result.width =
        static_cast<quint32>(std::max(geometry.width(), theme_->metrics().minimumWorkspaceWidth));
    result.height =
        static_cast<quint32>(std::max(geometry.height(), theme_->metrics().minimumWorkspaceHeight));
    result.maximized = window_->isMaximized();
    if (const auto* screen = window_->screen(); screen && !screen->name().isEmpty()) {
        result.hasScreenName = true;
        result.screenName = screen->name();
    }
    return result;
}
bool AppearanceController::preview(const QString& mode) {
    if (mode != "system" && mode != "light" && mode != "dark") {
        emit warningChanged(tr("Choose System, Light, or Dark."));
        return false;
    }
    if (!loaded_ || request_ == Request::Load || request_ == Request::Reset) {
        emit warningChanged(tr("Appearance is still loading."));
        return false;
    }
    if (!canSave()) {
        emit warningChanged(persistentWarning_);
        return false;
    }
    if (saveTimer_->isActive()) {
        saveTimer_->stop();
        dirty_ = true;
    }
    previewing_ = true;
    theme_->setMode(themeMode(mode));
    emit warningChanged(resetPreview_ ? QString{} : persistentWarning_);
    return true;
}
void AppearanceController::cancelPreview() {
    resetPreview_ = false;
    apply(persisted_, false);
    emit warningChanged(persistentWarning_);
    emit readyChanged(loaded_);
    previewing_ = false;
    if (dirty_)
        scheduleSave();
}
void AppearanceController::applyPreview() {
    if (!canSave()) {
        emit saveFinished(false, persistentWarning_.isEmpty()
                                     ? tr("Retry loading appearance or reset it before saving.")
                                     : persistentWarning_);
        return;
    }
    if (token_) {
        emit saveFinished(false, tr("Layout settings are still being saved. Please retry."));
        return;
    }
    // Keep the preview isolated from automatic layout writes until acknowledged.
    dirty_ = false;
    saveTimer_->stop();
    submitSave(Request::Save);
}
void AppearanceController::stageReset() {
    if (!loaded_ || token_) {
        emit warningChanged(tr("Appearance settings are still loading or saving. Please retry."));
        return;
    }
    saveTimer_->stop();
    resetPreview_ = true;
    previewing_ = true;
    theme_->setMode(design::ThemeMode::System);
    // Keep geometry unchanged until Save is acknowledged so Close can discard
    // the entire reset without overwriting compatible persisted placement.
    emit resolvedChoicesChanged(current());
    emit warningChanged({});
    emit readyChanged(true);
}
void AppearanceController::reset() {
    if (token_)
        return;
    previewing_ = false;
    token_ = nextToken();
    request_ = Request::Reset;
    adapter_->resetAppearanceLayout(token_);
}
void AppearanceController::resetLayout() {
    if (!canSave() || token_ || previewing_) {
        if (previewing_)
            emit warningChanged(
                tr("Apply or cancel the appearance preview before resetting layout."));
        return;
    }
    auto value = current();
    defaultSidebar_ = true;
    const auto defaults = defaultLayout();
    value.navigatorWidth = defaults.navigatorWidth;
    value.editorResultsSplit = defaults.editorResultsSplit;
    value.historyHeight = defaults.historyHeight;
    value.navigatorVisible = defaults.navigatorVisible;
    value.historyVisible = defaults.historyVisible;
    value.x = defaults.x;
    value.y = defaults.y;
    value.width = defaults.width;
    value.height = defaults.height;
    value.maximized = false;
    value.hasScreenName = false;
    value.screenName.clear();
    apply(value, true);
    dirty_ = false;
    submitSave(Request::AutomaticSave, value);
}
void AppearanceController::retry() {
    if (token_)
        return;
    token_ = nextToken();
    request_ = Request::Load;
    adapter_->getAppearanceLayout(token_);
}
void AppearanceController::apply(const AppearanceLayout& value, bool includeLayout) {
    applying_ = true;
    theme_->setMode(themeMode(value.theme));
    if (includeLayout) {
        if (!value.maximized && window_->isMaximized())
            window_->showNormal();
        const QRect requested(value.x, value.y, static_cast<int>(value.width),
                              static_cast<int>(value.height));
        bool onScreen = false;
        for (const auto* screen : QGuiApplication::screens())
            if (screen->availableGeometry().intersects(requested)) {
                onScreen = true;
                break;
            }
        if (onScreen)
            window_->setGeometry(requested);
        navigator_->show();
        window_->resizeDocks({navigator_}, {static_cast<int>(value.navigatorWidth)},
                             Qt::Horizontal);

        workspace_->setSizes({static_cast<int>(value.editorResultsSplit),
                              1000 - static_cast<int>(value.editorResultsSplit)});
        if (value.maximized)
            window_->showMaximized();
    }
    applying_ = false;
}
void AppearanceController::scheduleSave() {
    if (!loaded_ || !automaticAllowed_ || applying_ || previewing_)
        return;
    dirty_ = true;
    if (request_ == Request::None)
        saveTimer_->start();
}
void AppearanceController::submitSave(Request request, std::optional<AppearanceLayout> layout) {
    if (!loaded_ || token_)
        return;
    token_ = nextToken();
    request_ = request;
    if (request == Request::AutomaticSave)
        dirty_ = false;
    auto value = layout.value_or(current());
    if (request == Request::Save && resetPreview_) {
        const auto selectedTheme = value.theme;
        value = defaultLayout();
        value.theme = selectedTheme;
    }
    if (!adapter_->setAppearanceLayout(value, token_)) {
        token_ = 0;
        request_ = Request::None;
    }
}
bool AppearanceController::flush() {
    if (previewing_)
        cancelPreview();
    if (!token_ && !dirty_ && !saveTimer_->isActive())
        return true;
    flushRequested_ = true;
    if (request_ == Request::None && (dirty_ || saveTimer_->isActive())) {
        saveTimer_->stop();
        submitSave(Request::AutomaticSave);
    }
    return false;
}
bool AppearanceController::eventFilter(QObject* watched, QEvent* event) {
    // A stored width is an explicit compatible layout choice. Only fresh/reset
    // defaults respond to the reference breakpoint; dragging the native divider
    // turns that default into the user's own width.
    if (defaultSidebar_ && watched == window_ && event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const auto dock = navigator_->geometry();
        if (mouse->button() == Qt::LeftButton && mouse->position().y() >= dock.top() &&
            mouse->position().y() <= dock.bottom() && mouse->position().x() >= dock.right() &&
            mouse->position().x() <= dock.right() + theme_->metrics().spacingLarge)
            defaultSidebar_ = false;
    }
    if (defaultSidebar_ && loaded_ && !applying_ && !sidebarResizePending_ && watched == window_ &&
        event->type() == QEvent::Resize) {
        sidebarResizePending_ = true;
        QTimer::singleShot(0, this, [this] {
            sidebarResizePending_ = false;
            if (!defaultSidebar_)
                return;
            const auto metrics = theme_->metrics();
            const int width = window_->width() <= metrics.narrowWorkspaceWidth
                                  ? metrics.narrowNavigatorWidth
                                  : metrics.initialNavigatorWidth;
            const bool wasApplying = applying_;
            applying_ = true;
            window_->resizeDocks({navigator_}, {width}, Qt::Horizontal);
            applying_ = wasApplying;
        });
    }
    if ((watched == navigator_ && event->type() == QEvent::Resize) ||
        (watched == window_ && (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
                                event->type() == QEvent::WindowStateChange)))
        scheduleSave();
    return QObject::eventFilter(watched, event);
}
} // namespace choscordb
