#pragma once
#include "bridge/engine_adapter.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include <QPointer>
#include <QTimer>
#include <optional>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QStandardItemModel;
class QTableView;
class QWidget;
namespace choscordb {
namespace design {
class Button;
class FieldValidation;
class StatusLine;
}
struct BridgeEvent;
class TableImportDialog final : public DialogShell {
    Q_OBJECT
  public:
    TableImportDialog(EngineAdapter* adapter, quint64 connection, TableImportRequest request,
                      const QString& targetName, QWidget* parent = nullptr);
    ~TableImportDialog() override;
    bool isRunning() const;

  signals:
    void imported(quint64 connection);

  protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

  private:
    struct MappingRow {
        QString target;
        QCheckBox* include = nullptr;
        QLineEdit* name = nullptr;
        QComboBox* source = nullptr;
    };
    TableImportRequest request() const;
    void refreshPreview();
    void rebuildMapping();
    void updateReview();
    void updateActions();
    void start();
    void cancel();
    void handleEvent(const BridgeEvent& event);
    bool apply(ExportEvent event, const QString& detail = {});
    void showState(const QString& detail = {});
    void settle(ExportEvent event, const QString& detail = {});
    QPointer<EngineAdapter> adapter_;
    quint64 connection_;
    TableImportRequest base_;
    std::optional<quint64> import_;
    quint64 targetToken_ = 0;
    ExportState state_ = ExportState::Idle;
    bool closeAfter_ = false;
    QStringList fileColumns_;
    QStringList tableColumns_;
    QString qualifiedName_;
    QStringList keyColumns_;
    QString targetError_;
    QList<MappingRow> mapping_;
    QTimer previewTimer_;
    QLineEdit* path_;
    design::Button* browse_;
    QComboBox* format_;
    QComboBox* encoding_;
    QWidget* csvFields_;
    QLineEdit* delimiter_;
    QLineEdit* quote_;
    QLineEdit* nullText_;
    QCheckBox* header_;
    QWidget* tableNameRow_;
    QLineEdit* tableName_;
    QWidget* conflictRow_;
    QComboBox* conflict_;
    QStandardItemModel* previewModel_;
    QTableView* preview_;
    QLabel* previewSummary_ = nullptr;
    QWidget* mappingFields_;
    QFormLayout* mappingForm_ = nullptr;
    QPlainTextEdit* sqlPreview_;
    design::FieldValidation* validation_ = nullptr;
    design::StatusLine* statusLine_ = nullptr;
    design::Button* cancel_ = nullptr;
    design::Button* start_ = nullptr;
};
}
