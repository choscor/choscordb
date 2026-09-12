#include "app/appearance_controller.h"
#include "design_system/theme_manager.h"
#include <QDockWidget>
#include <QEvent>
#include <QGuiApplication>
#include <QMainWindow>
#include <QScreen>
#include <QSplitter>
#include <QTimer>
#include <algorithm>
#include <atomic>

namespace choscordb {
namespace {
design::ThemeMode themeMode(const QString& value) {
    if (value == "dark")
        return design::ThemeMode::Dark;
    if (value == "light")
        return design::ThemeMode::Light;
    return design::ThemeMode::System;
}
design::Density density(const QString& value) {
    return value == "comfortable" ? design::Density::Comfortable : design::Density::Compact;
}
design::AccentPreset accentPreset(const QString& value) {
    if (value == "azure")
        return design::AccentPreset::Azure;
    if (value == "teal")
        return design::AccentPreset::Teal;
    if (value == "green")
        return design::AccentPreset::Green;
    if (value == "violet")
        return design::AccentPreset::Violet;
    if (value == "orange")
        return design::AccentPreset::Orange;
    if (value == "rose")
        return design::AccentPreset::Rose;
    return design::AccentPreset::Cobalt;
}
QString accentName(design::AccentPreset value) {
    switch (value) {
    case design::AccentPreset::Teal:
        return QStringLiteral("teal");
    case design::AccentPreset::Azure:
        return QStringLiteral("azure");
    case design::AccentPreset::Green:
        return QStringLiteral("green");
    case design::AccentPreset::Violet:
        return QStringLiteral("violet");
    case design::AccentPreset::Orange:
        return QStringLiteral("orange");
    case design::AccentPreset::Rose:
        return QStringLiteral("rose");
    case design::AccentPreset::Cobalt:
        return QStringLiteral("cobalt");
    }
    return QStringLiteral("cobalt");
}
} // namespace

quint64 AppearanceController::nextToken() {
    static std::atomic<quint64> token{quint64(1) << 55};
    return token.fetch_add(1);
}
AppearanceController::AppearanceController(design::ThemeManager* theme, EngineAdapter* adapter,
                                           QMainWindow* window, QDockWidget* navigator,
                                           QSplitter* workspace, QDockWidget* history)
    : QObject(window), theme_(theme), adapter_(adapter), window_(window), navigator_(navigator),
      history_(history), workspace_(workspace), saveTimer_(new QTimer(this)) {
    connect(theme_, &design::ThemeManager::accessibilityPolicyChanged, this,
            &AppearanceController::accessibilityPolicyChanged);
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(300);
    connect(saveTimer_, &QTimer::timeout, this, [this] { submitSave(Request::AutomaticSave); });
    connect(workspace_, &QSplitter::splitterMoved, this, [this] { scheduleSave(); });
    connect(navigator_, &QDockWidget::visibilityChanged, this, [this] { scheduleSave(); });
    connect(history_, &QDockWidget::visibilityChanged, this, [this] { scheduleSave(); });
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
                    persisted_ = hasSaved ? value : AppearanceLayout{};
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
                    if (request == Request::Save)
                        apply(persisted_, false);
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
                    emit flushReady();
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
    result.density = theme_->density() == design::Density::Comfortable
                         ? QStringLiteral("comfortable")
                         : QStringLiteral("compact");
    const auto accent = theme_->accent();
    result.accentKind = accent.isCustom() ? QStringLiteral("custom") : QStringLiteral("preset");
    result.accent = accent.isCustom() ? accent.customColor.name() : accentName(accent.preset);
    result.navigatorWidth = static_cast<quint32>(
        std::max(navigator_->width(), theme_->metrics().minimumNavigatorWidth));
    const auto sizes = workspace_->sizes();
    const int total = sizes.size() >= 2 ? sizes[0] + sizes[1] : 0;
    if (total > 0)
        result.editorResultsSplit =
            static_cast<quint16>(std::clamp((sizes[0] * 1000) / total, 100, 900));
    result.historyHeight =
        static_cast<quint32>(std::max(history_->height(), theme_->metrics().minimumHistoryHeight));
    result.navigatorVisible = navigator_->isVisible();
    result.historyVisible = history_->isVisible();
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
bool AppearanceController::preview(const QString& mode, const QString& densityValue,
                                   const QString& accentKind, const QString& accentValue) {
    if (!loaded_ || request_ == Request::Load || request_ == Request::Reset) {
        emit warningChanged(tr("Appearance is still loading."));
        return false;
    }
    if (saveTimer_->isActive()) {
        saveTimer_->stop();
        dirty_ = true;
    }
    previewing_ = true;
    theme_->setMode(themeMode(mode));
    theme_->setDensity(density(densityValue));
    const auto selected = accentKind == "custom"
                              ? design::Accent::custom(QColor(accentValue))
                              : design::Accent::presetColor(accentPreset(accentValue));
    const auto validation = theme_->setAccent(selected);
    emit warningChanged(validation.accepted ? persistentWarning_ : validation.reason);
    return validation.accepted;
}
void AppearanceController::cancelPreview() {
    apply(persisted_, false);
    previewing_ = false;
    if (dirty_)
        scheduleSave();
}
void AppearanceController::applyPreview() {
    previewing_ = false;
    dirty_ = false;
    saveTimer_->stop();
    submitSave(Request::Save);
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
    if (!loaded_ || token_ || previewing_) {
        if (previewing_)
            emit warningChanged(
                tr("Apply or cancel the appearance preview before resetting layout."));
        return;
    }
    auto value = current();
    const AppearanceLayout defaults;
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
    submitSave(Request::AutomaticSave);
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
    theme_->setDensity(density(value.density));
    const auto selected = value.accentKind == "custom"
                              ? design::Accent::custom(QColor(value.accent))
                              : design::Accent::presetColor(accentPreset(value.accent));
    const auto validation = theme_->setAccent(selected);
    if (!validation.accepted)
        emit warningChanged(validation.reason);
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
        navigator_->setVisible(value.navigatorVisible);
        history_->setVisible(value.historyVisible);
        window_->resizeDocks({navigator_}, {static_cast<int>(value.navigatorWidth)},
                             Qt::Horizontal);
        window_->resizeDocks({history_}, {static_cast<int>(value.historyHeight)}, Qt::Vertical);
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
void AppearanceController::submitSave(Request request) {
    if (!loaded_ || token_)
        return;
    token_ = nextToken();
    request_ = request;
    if (request == Request::AutomaticSave)
        dirty_ = false;
    if (!adapter_->setAppearanceLayout(current(), token_)) {
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
    if (watched == window_ && (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
                               event->type() == QEvent::WindowStateChange))
        scheduleSave();
    return QObject::eventFilter(watched, event);
}
} // namespace choscordb
