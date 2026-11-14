#pragma once
#include "app/result_column_widths.h"
#include "bridge/engine_adapter.h"
#include <QList>
#include <QObject>
#include <memory>
#include <optional>

class QAbstractButton;
class QAction;
class QDockWidget;
class QMainWindow;
class QStackedWidget;
class QSplitter;
class QTimer;

namespace choscordb {
namespace design {
class ThemeManager;
}
struct SidebarLayout {
    QAction* visible;
    QStackedWidget* panels;
    QList<QAbstractButton*> tabs;
};
class AppearanceController final : public QObject {
    Q_OBJECT
  public:
    AppearanceController(design::ThemeManager* theme, EngineAdapter* adapter, QMainWindow* window,
                         QDockWidget* navigator, QSplitter* workspace, SidebarLayout sidebar,
                         std::shared_ptr<ResultColumnWidths> columnWidths);
    [[nodiscard]] AppearanceLayout persisted() const { return persisted_; }
    [[nodiscard]] AppearanceLayout current() const;
    [[nodiscard]] bool isReady() const { return loaded_; }
    [[nodiscard]] bool canSave() const { return loaded_ && (automaticAllowed_ || resetPreview_); }
    [[nodiscard]] QString currentWarning() const { return persistentWarning_; }
    [[nodiscard]] bool forcedContrast() const;
    [[nodiscard]] bool reducedMotion() const;
    [[nodiscard]] bool preview(const QString& theme);
    void cancelPreview();
    void applyPreview();
    void reset();
    void stageReset();
    void resetLayout();
    void retry();
    [[nodiscard]] bool flush();
    void scheduleSave();
  signals:
    void warningChanged(const QString& warning);
    void saveFinished(bool success, const QString& message);
    void readyChanged(bool ready);
    void resolvedChoicesChanged(const choscordb::AppearanceLayout& appearance);
    void accessibilityPolicyChanged(bool forcedContrast, bool reducedMotion);
    void flushReady();
    void flushFailed(const QString& message);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    enum class Request { None, Load, Save, Reset, AutomaticSave };
    void apply(const AppearanceLayout& value, bool includeLayout);
    void submitSave(Request request, std::optional<AppearanceLayout> layout = std::nullopt);
    design::ThemeManager* theme_;
    EngineAdapter* adapter_;
    QMainWindow* window_;
    QDockWidget* navigator_;
    QSplitter* workspace_;
    SidebarLayout sidebar_;
    std::shared_ptr<ResultColumnWidths> columnWidths_;
    QTimer* saveTimer_;
    AppearanceLayout persisted_;
    quint64 token_ = 0;
    Request request_ = Request::None;
    bool loaded_ = false, applying_ = false, previewing_ = false;
    bool automaticAllowed_ = false, dirty_ = false, resetPreview_ = false;
    bool flushRequested_ = false;
    bool defaultSidebar_ = false;
    bool sidebarResizePending_ = false;
    QString persistentWarning_;
};
}
