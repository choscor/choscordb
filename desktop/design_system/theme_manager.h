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
    [[nodiscard]] ResolvedTheme resolvedTheme() const;
    [[nodiscard]] bool forcedContrast() const;
    [[nodiscard]] bool reducedMotion() const;

    void setMode(ThemeMode mode);

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
    void accessibilityPolicyChanged(bool forcedContrast, bool reducedMotion);

  private:
    [[nodiscard]] ResolvedTheme resolveTheme() const;
    void refreshTheme();

    ThemeMode mode_ = ThemeMode::System;
    ResolvedAppearance systemAppearance_ = ResolvedAppearance::Light;
    QPalette systemPalette_;
    ResolvedTheme resolvedTheme_;
    bool forcedContrast_ = false;
    bool reducedMotion_ = false;
    QApplication* application_ = nullptr;
};

} // namespace choscordb::design
