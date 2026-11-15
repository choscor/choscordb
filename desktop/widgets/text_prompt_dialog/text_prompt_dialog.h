#pragma once
#include "design_system/dialog_shell/dialog_shell.h"
#include <functional>

class QLineEdit;
namespace choscordb {
namespace design {
class Button;
class FieldValidation;
}
struct TextPrompt {
    QString objectName, title, description, label, text, accept;
};
class TextPromptDialog final : public DialogShell {
    Q_OBJECT
  public:
    using Work = std::function<QString(const QString&)>;
    TextPromptDialog(const TextPrompt& prompt, Work work, QWidget* parent = nullptr);

  signals:
    void applied(const QString& text);

  private:
    void submit();
    Work work_;
    QLineEdit* field_;
    design::FieldValidation* validation_ = nullptr;
    design::Button* accept_ = nullptr;
    bool busy_ = false;
};
}
