#pragma once
#include "bridge/engine_adapter.h"
#include "widgets/dialog_shell.h"
#include <QPointer>
class QSpinBox;
class QLabel;
namespace choscordb {
namespace design {
class Button;
}
class QuerySettingsDialog final : public DialogShell {
    Q_OBJECT
  public:
    explicit QuerySettingsDialog(EngineAdapter* adapter, QWidget* parent = nullptr);
  signals:
    void queryPreferencesConfirmed(const choscordb::QueryPreferences& value);
    void queryPreferencesSaveSubmitted(quint64 token);

  private:
    void apply();
    void fill(const QueryPreferences& value);
    void updateControls();
    QPointer<EngineAdapter> adapter_;
    QSpinBox *pageSize_, *timeout_;
    design::Button *apply_, *reset_;
    QLabel* status_;
    quint64 token_ = 0;
    bool ready_ = false, saving_ = false;
};
} // namespace choscordb
