#pragma once
#include "bridge/engine_adapter.h"
#include "models/result_table_model.h"
#include <QWidget>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace choscordb {
class ResultFilterBar final : public QWidget {
    Q_OBJECT
  public:
    explicit ResultFilterBar(QWidget* parent = nullptr);
    void setColumns(const std::vector<ResultColumn>& columns,
                    const std::vector<ResultTableModel::Row>& samples = {});
    QList<ResultFilterCondition> conditions() const { return conditions_; }
    bool draftMatches(const QList<ResultFilterCondition>& conditions) const;
    void setBusy(bool busy);
    void reset();
    void markApplied(const QList<ResultFilterCondition>& conditions);
    void restoreApplied();
    bool hasAppliedFilters() const { return !applied_.isEmpty(); }

  signals:
    void applyRequested(const QList<choscordb::ResultFilterCondition>& conditions);
    void clearRequested();

  private:
    void updateOperators();
    void updateValueState();
    void addCondition();
    void removeCondition();
    QString valueKind() const;
    bool validValue(QString* error) const;
    void refreshList();
    QComboBox* column_;
    QComboBox* operation_;
    QLineEdit* value_;
    QPushButton* add_;
    QPushButton* remove_;
    QPushButton* apply_;
    QPushButton* clear_;
    QListWidget* list_;
    QLabel* error_;
    std::vector<ResultColumn> columns_;
    QStringList columnKinds_;
    QList<ResultFilterCondition> conditions_;
    QList<ResultFilterCondition> applied_;
};
} // namespace choscordb
