#pragma once
#include "bridge/engine_adapter.h"
#include "design_system/dialog_shell/dialog_shell.h"

namespace choscordb {
namespace design {
class Button;
class StatusLine;
}
class StructureEditor;
class StructureEditorDialog final : public DialogShell {
    Q_OBJECT
  public:
    StructureEditorDialog(EngineAdapter* adapter, quint64 connection, StructureTable table,
                          const QString& targetName, QWidget* parent = nullptr);

  signals:
    void applied(quint64 connection);

  protected:
    void reject() override;

  private:
    void updateActions();
    void showStatus(const QString& message, bool error, bool busy);
    StructureEditor* editor_ = nullptr;
    design::StatusLine* statusLine_ = nullptr;
    design::Button* cancel_ = nullptr;
    design::Button* apply_ = nullptr;
};
}
