#pragma once
#include "bridge/engine_adapter.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include <QPointer>
#include <optional>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QWidget;
namespace choscordb {
namespace design {
class Button;
class FieldValidation;
class StatusLine;
}
struct BridgeEvent;
class TableExportDialog final : public DialogShell {
    Q_OBJECT
  public:
    TableExportDialog(EngineAdapter* adapter, quint64 connection, TableExportRequest request,
                      QWidget* parent = nullptr);
    ~TableExportDialog() override;
    bool isRunning() const;

  protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

  private:
    TableExportRequest request() const;
    void updateActions();
    void start();
    void cancel();
    void handleEvent(const BridgeEvent& event);
    bool apply(ExportEvent event, const QString& detail = {});
    void showState(const QString& detail = {});
    void settle(ExportEvent event, const QString& detail = {});
    QPointer<EngineAdapter> adapter_;
    quint64 connection_;
    TableExportRequest base_;
    std::optional<quint64> export_;
    ExportState state_ = ExportState::Idle;
    bool closeAfter_ = false;
    QComboBox* format_;
    QWidget* csvFields_;
    QLineEdit* delimiter_;
    QLineEdit* quote_;
    QComboBox* lineBreak_;
    QLineEdit* nullText_;
    QCheckBox* header_;
    QWidget* sqlFields_;
    QCheckBox* structure_;
    QCheckBox* drop_;
    QCheckBox* data_;
    QComboBox* target_;
    QLineEdit* path_;
    design::Button* browse_;
    QWidget* patternRow_;
    QLineEdit* pattern_;
    QListWidget* tables_;
    design::FieldValidation* tablesValidation_ = nullptr;
    design::FieldValidation* optionsValidation_ = nullptr;
    design::FieldValidation* validation_;
    design::StatusLine* statusLine_ = nullptr;
    design::Button* cancel_ = nullptr;
    design::Button* start_ = nullptr;
};
}
