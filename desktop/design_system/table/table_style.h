#pragma once

#include <QModelIndex>
#include <QPointer>
#include <QString>
#include <QStyledItemDelegate>
class QTableView;
class QAction;
class QComboBox;
class QItemSelectionModel;
class QAbstractItemModel;

namespace choscordb::design {
// Neutral cell presentation roles. Producers retain all eligibility decisions.
inline constexpr int ChoiceLabelsRole = Qt::UserRole + 20;
inline constexpr int ChoiceNullableRole = Qt::UserRole + 21;
inline constexpr int ForeignKeyLinkLabelRole = Qt::UserRole + 22;
inline constexpr int TypedNullEditRole = Qt::UserRole + 23;
inline constexpr int CellNullRole = Qt::UserRole + 24;

class ResultTableDelegate final : public QStyledItemDelegate {
    Q_OBJECT
  public:
    explicit ResultTableDelegate(QTableView& table);
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                   const QModelIndex& index) override;

  signals:
    void linkActivated(const QModelIndex& index);

  private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void bindSelectionModel();
    void updateLinkAction();
    QTableView& table_;
    QAction* linkAction_ = nullptr;
    QPointer<QItemSelectionModel> selectionModel_;
    QPointer<QAbstractItemModel> model_;
};

QString tableStyleSheet();
QString tableItemStyleSheet();
ResultTableDelegate* configureResultTable(QTableView& table, bool showGrid = true);
} // namespace choscordb::design
