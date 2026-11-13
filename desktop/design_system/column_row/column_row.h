#pragma once

#include <QStyledItemDelegate>

namespace choscordb::design {
enum class RowMark { None, Loading, Failed, Unavailable };

class ColumnRowDelegate : public QStyledItemDelegate {
  public:
    explicit ColumnRowDelegate(int detailRole, QObject* parent = nullptr, int markRole = -1);
    void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const override;

  private:
    int detailRole_;
    int markRole_;
};
}
