#pragma once
#include "bridge/value_preview.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include <QByteArray>

class QPlainTextEdit;
namespace choscordb {
class ValuePreviewDialog final : public DialogShell {
    Q_OBJECT
  public:
    ValuePreviewDialog(const ValuePreview& preview, const QByteArray& bytes,
                       QWidget* parent = nullptr);

  private:
    QPlainTextEdit* text_ = nullptr;
};
}
