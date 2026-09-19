#pragma once
#include "bridge/engine_adapter.h"
#include <QPointer>
#include <QWidget>
class QCheckBox;
class QLineEdit;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTableView;
namespace choscordb {
namespace design {
class Button;
}
class HistoryModel;
class HistoryDock final : public QWidget {
    Q_OBJECT
  public:
    explicit HistoryDock(EngineAdapter* adapter, QWidget* parent = nullptr);
  public slots:
    void refresh();
    void applyConfirmedPolicy(const choscordb::HistoryPolicy& policy);
  signals:
    void openRequested(const choscordb::SavedHistoryEntry& entry);

  private:
    void loadPage(quint32 offset);
    void applyFilter();
    void selectEntry();
    void renderPreview();
    void updateControls();
    void openSelection();
    QPointer<EngineAdapter> adapter_;
    HistoryModel* model_;
    QTableView* table_;
    QPlainTextEdit* preview_;
    QLabel *status_, *previewNotice_, *page_;
    QCheckBox* record_;
    QLineEdit* search_;
    QComboBox* statusFilter_;
    design::Button *clear_, *refresh_, *previous_, *next_, *open_;
    design::Button *previewPrevious_, *previewNext_;
    HistoryPolicy policy_;
    quint64 listToken_ = 0, policyToken_ = 0, clearToken_ = 0, profilesToken_ = 0;
    quint32 offset_ = 0, pendingOffset_ = 0;
    QList<quint32> visitedOffsets_;
    QList<qsizetype> previewOffsets_;
    qsizetype previewOffset_ = 0, previewLength_ = 0;
    bool havePolicy_ = false;
    bool failed_ = false;
};
} // namespace choscordb
