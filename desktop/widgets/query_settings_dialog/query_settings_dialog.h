#pragma once
#include "bridge/engine_adapter.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include <QPointer>
class QSpinBox;
class QLabel;
namespace choscordb {
namespace design {
class Button;
class StatusLine;
} // namespace design
class QuerySettingsDialog final : public DialogShell {
    Q_OBJECT
  public:
    explicit QuerySettingsDialog(EngineAdapter* adapter, QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* event) override;

  signals:
    void queryPreferencesConfirmed(const choscordb::QueryPreferences& value);
    void queryPreferencesSaveSubmitted(quint64 token);

  private:
    void setStatus(const QString& message);
    void apply();
    void fill(const QueryPreferences& value);
    void updateControls();
    design::StatusLine* statusLine_;
    QLabel* status_;
    QPointer<EngineAdapter> adapter_;
    QSpinBox *pageSize_, *timeout_;
    quint32 connectionTimeoutSeconds_;
    bool showSystemSchemas_ = false;
    design::Button *apply_, *reset_;
    quint64 token_ = 0;
    bool ready_ = false, saving_ = false;
};
} // namespace choscordb
