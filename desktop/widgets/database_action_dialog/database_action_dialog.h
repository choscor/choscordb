#pragma once
#include "bridge/engine_adapter.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include <QPointer>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
namespace choscordb {
namespace design {
class Button;
class StatusLine;
}
struct BridgeEvent;
class DatabaseActionDialog final : public DialogShell {
    Q_OBJECT
  public:
    DatabaseActionDialog(EngineAdapter* adapter, quint64 connection, const QString& driver,
                         const DatabaseList& databases, QWidget* parent = nullptr);
    bool isApplying() const { return applyToken_ != 0; }

  signals:
    void applied(quint64 connection, const QString& label);

  protected:
    void reject() override;

  private:
    DatabaseAction action() const;
    DatabaseActionKindInfo kind() const;
    void updateFields();
    void updateReview();
    void apply();
    void handleEvent(const BridgeEvent& event);
    void showStatus(const QString& message, bool error, bool busy);
    QPointer<EngineAdapter> adapter_;
    quint64 connection_;
    QString driver_;
    QString current_;
    QList<DatabaseActionKindInfo> kinds_;
    quint64 applyToken_ = 0;
    QFormLayout* form_ = nullptr;
    QComboBox* kind_ = nullptr;
    QComboBox* target_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* newName_ = nullptr;
    QLineEdit* encoding_ = nullptr;
    QLineEdit* collation_ = nullptr;
    QLineEdit* confirmation_ = nullptr;
    QLabel* error_ = nullptr;
    QPlainTextEdit* sqlPreview_ = nullptr;
    design::StatusLine* statusLine_ = nullptr;
    design::Button* cancel_ = nullptr;
    design::Button* apply_ = nullptr;
};
}
