#pragma once

#include "design_system/theme.h"

#include <QObject>

class QApplication;
class QWidget;

namespace choscordb::design {

class ThemeManager final : public QObject {
    Q_OBJECT

  public:
    explicit ThemeManager(QObject* parent = nullptr);

    [[nodiscard]] ThemeMode mode() const;
    [[nodiscard]] Density density() const;
    [[nodiscard]] Accent accent() const;
    [[nodiscard]] ResolvedTheme resolvedTheme() const;
    [[nodiscard]] DesignMetrics metrics() const;
    [[nodiscard]] bool forcedContrast() const;
    [[nodiscard]] bool reducedMotion() const;

    void setMode(ThemeMode mode);
    void setDensity(Density density);
    [[nodiscard]] AccentValidation setAccent(const Accent& accent);

    // These hooks are fed by the platform integration layer. User choices are
    // retained while accessibility policy temporarily takes precedence.
    void setSystemAppearance(ResolvedAppearance appearance);
    void setSystemPalette(const QPalette& palette);
    void setForcedContrast(bool enabled);
    void setReducedMotion(bool enabled);
    void applyTo(QApplication& application) const;
    void applyTo(QWidget& topLevelWidget) const;
    void installOn(QApplication* application);

  signals:
    void themeChanged(const choscordb::design::ResolvedTheme& theme);
    void metricsChanged(const choscordb::design::DesignMetrics& metrics);
    void accessibilityPolicyChanged(bool forcedContrast, bool reducedMotion);

  private:
    [[nodiscard]] ResolvedTheme resolveTheme() const;
    void refreshTheme();

    ThemeMode mode_ = ThemeMode::System;
    Density density_ = Density::Compact;
    Accent accent_;
    ResolvedAppearance systemAppearance_ = ResolvedAppearance::Light;
    QPalette systemPalette_;
    ResolvedTheme resolvedTheme_;
    DesignMetrics metrics_;
    bool forcedContrast_ = false;
    bool reducedMotion_ = false;
    QApplication* application_ = nullptr;
};

} // namespace choscordb::design
