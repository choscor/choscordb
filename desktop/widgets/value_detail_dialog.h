#pragma once
#include <QDialog>
#include <QPointer>
#include <optional>

class QLabel;
class QPushButton;
class QTableView;
namespace choscordb {
class EngineAdapter;
class ValuePreviewModel;
struct BridgeEvent;
class ValueDetailDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit ValueDetailDialog(EngineAdapter* adapter, QWidget* parent = nullptr);
    ~ValueDetailDialog() override;
    void openValue(quint64 query, quint64 handle, const QString& type = {},
                   std::optional<quint64> length = std::nullopt);
    void clearValue();

  protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

  private:
    void request(quint64 offset, quint32 maxBytes = 65536);
    void previousChunk();
    void sizeVisibleColumns();
    void event(const BridgeEvent& event);
    void dropChunk();
    void fail(const QString& error);
    void updateActions();
    QPointer<EngineAdapter> adapter_;
    ValuePreviewModel* model_;
    QTableView* table_;
    QLabel* status_;
    QPushButton* previous_;
    QPushButton* next_;
    std::optional<quint64> query_;
    quint64 handle_ = 0;
    quint64 offset_ = 0;
    quint64 total_ = 0;
    quint64 nextOffset_ = 0;
    quint64 windowBytes_ = 65536;
    bool loading_ = false;
    bool hasChunk_ = false;
    std::optional<quint64> lease_;
    std::optional<quint64> alignmentTarget_;
};
} // namespace choscordb
