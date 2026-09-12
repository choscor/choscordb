#pragma once

#include <QObject>
#include <QPalette>
#include <QString>

namespace choscordb::design {

class ThemeManager;

struct PlatformAccessibilityPreferences final {
    bool forcedContrast = false;
    bool reducedMotion = false;

    friend bool operator==(const PlatformAccessibilityPreferences&,
                           const PlatformAccessibilityPreferences&) = default;
};

[[nodiscard]] PlatformAccessibilityPreferences readPlatformAccessibilityPreferences();

class PlatformAccessibilityMonitor final : public QObject {
    Q_OBJECT

  public:
    explicit PlatformAccessibilityMonitor(ThemeManager* theme, QObject* parent = nullptr);
    void refresh();

  private:
#if defined(Q_OS_LINUX)
    void requestLinuxPreference(const QString& key, bool PlatformAccessibilityPreferences::* field);
    int linuxReadsPending_ = 0;
#endif
    ThemeManager* theme_;
    PlatformAccessibilityPreferences current_;
    QPalette systemPalette_;
};

} // namespace choscordb::design
