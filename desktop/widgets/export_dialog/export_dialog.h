#pragma once
#include "bridge/engine_adapter.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include <QPointer>
#include <QStringList>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
namespace choscordb {
namespace design {
class Button;
class FieldValidation;
class StatusLine;
}
class EngineAdapter;
struct BridgeEvent;
class ExportDialog final : public DialogShell {
    Q_OBJECT
  public:
    explicit ExportDialog(EngineAdapter* adapter, QWidget* parent = nullptr);
    ~ExportDialog() override;
    void setQuery(quint64 query);
    void setResultViewActive(bool active);
    void clearQuery();
    void startExportToDialect(const QString& path, const QString& format, const QStringList& table,
                              const QString& dialect);
    bool isRunning() const;
  signals:
    void exportRunningChanged(bool running);

  protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

  private:
    void updateActions();
    void start();
    void cancel();
    void handleEvent(const BridgeEvent& event);
    bool apply(ExportEvent event, const QString& detail = {});
    void showState(const QString& detail = {});
    void settle(ExportEvent event, const QString& detail = {});
    QPointer<EngineAdapter> adapter_;
    std::optional<quint64> query_;
    std::optional<quint64> export_;
    std::optional<quint64> exportQuery_;
    quint64 submissionToken_ = 0;
    ExportState state_ = ExportState::Idle;
    bool closeAfter_ = false;
    QString submissionError_;
    QComboBox* format_;
    QComboBox* dialect_;
    QLineEdit* destination_;
    QLineEdit* schema_;
    QLineEdit* table_;
    QWidget* sqlFields_;
    design::Button* browse_;
    design::Button* start_;
    design::Button* cancel_;
    QLabel* scope_;
    QLabel* status_;
    design::StatusLine* statusLine_;
    design::FieldValidation *formatValidation_, *destinationValidation_, *tableValidation_;
};
}
