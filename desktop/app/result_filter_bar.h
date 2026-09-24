#pragma once
#include "bridge/engine_adapter.h"
#include "models/result_table_model.h"
#include <QWidget>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;
class QListWidget;
class QPushButton;

namespace choscordb {
class ResultFilterBar final : public QWidget {
    Q_OBJECT
  public:
    explicit ResultFilterBar(QWidget* parent = nullptr);
    ~ResultFilterBar() override;
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
    struct Row {
        QWidget* widget;
        QComboBox* column;
        QComboBox* operation;
        QLineEdit* value;
        QPushButton* remove;
    };
    void updateOperators(Row* row);
    void updateValueState(Row* row);
    void addCondition();
    void removeCondition(Row* row);
    QString valueKind(const Row* row) const;
    bool validValue(const Row* row, QString* error) const;
    void refreshList();
    void submit();
    void loadDraft(const QList<ResultFilterCondition>& conditions);
    void updateMode();
    QVBoxLayout* rowsLayout_;
    QList<Row*> rows_;
    QWidget* manual_;
    QLineEdit* sql_;
    QPushButton* mode_;
    QPushButton* add_;
    QPushButton* apply_;
    QPushButton* clear_;
    QListWidget* list_;
    QLabel* error_;
    bool busy_ = false;
    bool sqlMode_ = false;
    bool dirty_ = false;
    bool updating_ = false;
    std::vector<ResultColumn> columns_;
    QStringList columnKinds_;
    QList<ResultFilterCondition> applied_;
};
} // namespace choscordb
