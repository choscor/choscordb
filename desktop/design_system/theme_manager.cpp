#include "design_system/theme_manager.h"
#include "design_system/control_style.h"

#include <QApplication>
#include <QWidget>

namespace choscordb::design {

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent), metrics_(resolveMetrics(density_, false)) {
    // resolveTheme reads accessibility policy members declared after the theme.
    // Resolve only after all member initializers have established that policy.
    resolvedTheme_ = resolveTheme();
}

ThemeMode ThemeManager::mode() const {
    return mode_;
}
Density ThemeManager::density() const {
    return density_;
}
Accent ThemeManager::accent() const {
    return accent_;
}
ResolvedTheme ThemeManager::resolvedTheme() const {
    return resolvedTheme_;
}
DesignMetrics ThemeManager::metrics() const {
    return metrics_;
}
bool ThemeManager::forcedContrast() const {
    return forcedContrast_;
}
bool ThemeManager::reducedMotion() const {
    return reducedMotion_;
}

void ThemeManager::setMode(ThemeMode mode) {
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    refreshTheme();
}

void ThemeManager::setDensity(Density density) {
    if (density_ == density) {
        return;
    }
    density_ = density;
    metrics_ = resolveMetrics(density_, reducedMotion_);
    if (application_ != nullptr) {
        applyTo(*application_);
    }
    emit metricsChanged(metrics_);
}

AccentValidation ThemeManager::setAccent(const Accent& accent) {
    const auto appearance =
        mode_ == ThemeMode::System
            ? systemAppearance_
            : (mode_ == ThemeMode::Dark ? ResolvedAppearance::Dark : ResolvedAppearance::Light);
    auto validation = validateAccent(accent, appearance);
    if (validation.accepted && accent.isCustom()) {
        const auto otherAppearance = appearance == ResolvedAppearance::Light
                                         ? ResolvedAppearance::Dark
                                         : ResolvedAppearance::Light;
        validation = validateAccent(accent, otherAppearance);
    }
    if (!validation.accepted || accent_ == accent) {
        return validation;
    }
    accent_ = accent;
    refreshTheme();
    return validation;
}

void ThemeManager::setSystemAppearance(ResolvedAppearance appearance) {
    if (systemAppearance_ == appearance) {
        return;
    }
    systemAppearance_ = appearance;
    if (mode_ == ThemeMode::System) {
        refreshTheme();
    }
}

void ThemeManager::setSystemPalette(const QPalette& palette) {
    if (systemPalette_ == palette) {
        return;
    }
    systemPalette_ = palette;
    if (forcedContrast_) {
        refreshTheme();
    }
}

void ThemeManager::setForcedContrast(bool enabled) {
    if (forcedContrast_ == enabled) {
        return;
    }
    forcedContrast_ = enabled;
    refreshTheme();
    emit accessibilityPolicyChanged(forcedContrast_, reducedMotion_);
}

void ThemeManager::setReducedMotion(bool enabled) {
    if (reducedMotion_ == enabled) {
        return;
    }
    reducedMotion_ = enabled;
    metrics_ = resolveMetrics(density_, reducedMotion_);
    if (application_ != nullptr) {
        applyTo(*application_);
    }
    emit metricsChanged(metrics_);
    emit accessibilityPolicyChanged(forcedContrast_, reducedMotion_);
}

void ThemeManager::applyTo(QApplication& application) const {
    application.setProperty("designTheme", QVariant::fromValue(resolvedTheme_));
    application.setProperty("forcedContrast", resolvedTheme_.forcedContrast);
    application.setFont(resolveTypography(TypographyRole::Ui));
    application.setPalette(applicationPalette(resolvedTheme_));
    application.setStyleSheet(applicationStyleSheet(resolvedTheme_, metrics_));
}

void ThemeManager::applyTo(QWidget& topLevelWidget) const {
    topLevelWidget.setProperty("designTheme", QVariant::fromValue(resolvedTheme_));
    topLevelWidget.setProperty("forcedContrast", resolvedTheme_.forcedContrast);
    topLevelWidget.setFont(resolveTypography(TypographyRole::Ui));
    topLevelWidget.setStyleSheet(applicationStyleSheet(resolvedTheme_, metrics_));
    // Replacing an existing QSS can restore its cached base palette. Apply the
    // resolved palette afterward so scoped themes also update background paper.
    topLevelWidget.setPalette(applicationPalette(resolvedTheme_));
}

void ThemeManager::installOn(QApplication* application) {
    application_ = application;
    if (application_ != nullptr &&
        !application_->property("designControlStyleInstalled").toBool()) {
        application_->setStyle(new ControlStyle);
        application_->setProperty("designControlStyleInstalled", true);
    }
    if (application_ != nullptr) {
        applyTo(*application_);
    }
}

ResolvedTheme ThemeManager::resolveTheme() const {
    const auto appearance =
        mode_ == ThemeMode::System
            ? systemAppearance_
            : (mode_ == ThemeMode::Dark ? ResolvedAppearance::Dark : ResolvedAppearance::Light);
    return {appearance,
            forcedContrast_ ? resolveForcedContrastColors(systemPalette_)
                            : resolveColors(appearance, accent_),
            forcedContrast_};
}

void ThemeManager::refreshTheme() {
    const auto updated = resolveTheme();
    if (updated == resolvedTheme_) {
        return;
    }
    resolvedTheme_ = updated;
    if (application_ != nullptr) {
        applyTo(*application_);
    }
    emit themeChanged(resolvedTheme_);
}

} // namespace choscordb::design
