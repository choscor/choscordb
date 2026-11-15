#pragma once
#include "bridge/engine_adapter.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include <QPointer>
#include <optional>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
namespace choscordb {
namespace design {
class Button;
class FieldValidation;
class StatusLine;
}
struct BridgeEvent;
struct SqlFileTarget {
    quint64 connection = 0;
    QString driver;
    QString sqlMode;
    QString safety;
    QString name;
};
class SqlFileDialog final : public DialogShell {
    Q_OBJECT
  public:
    SqlFileDialog(EngineAdapter* adapter, SqlFileTarget target, QWidget* parent = nullptr);
    ~SqlFileDialog() override;
    bool isRunning() const;

  protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

  private:
    void start();
    void cancel();
    void handleEvent(const BridgeEvent& event);
    bool apply(ExportEvent event, const QString& detail = {});
    void showState(const QString& detail = {});
    void settle(ExportEvent event, const QString& detail = {});
    void updateActions();
    QPointer<EngineAdapter> adapter_;
    SqlFileTarget target_;
    std::optional<quint64> run_;
    ExportState state_ = ExportState::Idle;
    bool closeAfter_ = false;
    QLineEdit* path_;
    design::Button* browse_;
    QComboBox* mode_;
    QPlainTextEdit* errors_;
    design::FieldValidation* validation_ = nullptr;
    design::StatusLine* statusLine_ = nullptr;
    design::Button* cancel_ = nullptr;
    design::Button* start_ = nullptr;
};
}
