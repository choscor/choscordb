#pragma once
#include "bridge/engine_adapter.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include <QPointer>
#include <optional>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
namespace choscordb {
namespace design {
class Button;
class FieldValidation;
class StatusLine;
}
struct BridgeEvent;
struct BackupTarget {
    quint64 connection = 0;
    QString driver;
    QString safety;
    QString name;
    bool restore = false;
};
class BackupDialog final : public DialogShell {
    Q_OBJECT
  public:
    BackupDialog(EngineAdapter* adapter, BackupTarget target, QWidget* parent = nullptr);
    ~BackupDialog() override;
    bool isRunning() const;

  protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

  private:
    QWidget* pathRow(QLineEdit* field, design::FieldValidation** validation,
                     design::Button* browse);
    void start();
    void cancel();
    void handleEvent(const BridgeEvent& event);
    bool apply(ExportEvent event, const QString& detail = {});
    void showState(const QString& detail = {});
    void settle(ExportEvent event, const QString& detail = {});
    void updateActions();
    QPointer<EngineAdapter> adapter_;
    BackupTarget target_;
    BackupForm form_;
    std::optional<quint64> run_;
    ExportState state_ = ExportState::Idle;
    bool closeAfter_ = false;
    QLineEdit* path_;
    QLineEdit* toolFolder_;
    design::Button* browse_;
    design::Button* browseTools_;
    QList<QPair<QString, QCheckBox*>> options_;
    QPlainTextEdit* output_;
    design::FieldValidation* validation_ = nullptr;
    design::FieldValidation* toolValidation_ = nullptr;
    design::StatusLine* statusLine_ = nullptr;
    design::Button* cancel_ = nullptr;
    design::Button* start_ = nullptr;
};
}
