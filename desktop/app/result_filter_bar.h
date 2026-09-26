#pragma once
#include "bridge/engine_adapter.h"
#include "models/result_table_model.h"
#include <QWidget>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;

namespace choscordb {
class ResultFilterBar final : public QWidget {
    Q_OBJECT
  public:
    explicit ResultFilterBar(QWidget* parent = nullptr);
    void setColumns(const std::vector<ResultColumn>& columns,
                    const std::vector<ResultTableModel::Row>& samples = {});
    QList<ResultFilterCondition> conditions() const;
    bool draftMatches(const QList<ResultFilterCondition>& conditions) const;
    void setBusy(bool busy);
    void reset();
    void markApplied(const QList<ResultFilterCondition>& conditions);
    void restoreApplied();
    void showValidationError(const QString& error);
    bool hasAppliedFilters() const { return !applied_.isEmpty(); }

  signals:
    void applyRequested(const QList<choscordb::ResultFilterCondition>& conditions);
    void clearRequested();

  private:
    void submit();
    void loadDraft(const QList<ResultFilterCondition>& conditions);
    void refreshActions();
    QLineEdit* sql_;
    QPushButton* apply_;
    QPushButton* clear_;
    QLabel* error_;
    bool busy_ = false;
    bool hasColumns_ = false;
    QList<ResultFilterCondition> applied_;
};
} // namespace choscordb
