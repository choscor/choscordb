#pragma once
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
}
class EngineAdapter;
struct BridgeEvent;
class ExportDialog final : public DialogShell {
    Q_OBJECT
  public:
    explicit ExportDialog(EngineAdapter* adapter, QWidget* parent = nullptr);
    ~ExportDialog() override;
    void setQuery(quint64 query);
    void clearQuery();
    void startExportTo(const QString& path, const QString& format, const QStringList& table = {},
                       bool postgres = false);
    bool isRunning() const;
  signals:
    void exportRunningChanged(bool running);

  protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void reject() override;

  private:
    void updateActions();
    void start();
    void cancel();
    void handleEvent(const BridgeEvent& event);
    void finish(const QString& message, bool failed);
    QPointer<EngineAdapter> adapter_;
    std::optional<quint64> query_;
    std::optional<quint64> export_;
    std::optional<quint64> exportQuery_;
    quint64 submissionToken_ = 0;
    bool submitting_ = false;
    bool cancelling_ = false;
    bool closeAfter_ = false;
    QComboBox* format_;
    QComboBox* dialect_;
    QLineEdit* destination_;
    QLineEdit* schema_;
    QLineEdit* table_;
    QWidget* sqlFields_;
    design::Button* browse_;
    design::Button* start_;
    design::Button* cancel_;
    QLabel* status_;
};
} // namespace choscordb
