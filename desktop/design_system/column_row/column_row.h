#pragma once

#include <QStyledItemDelegate>

namespace choscordb::design {
// The icon after the name of a navigation row. A failed row also has a danger fill and name.
enum class RowMark { None, Loading, Failed, Unavailable };

// A single-line navigation row with an independent, right-aligned database type. A row
// whose `markRole` holds a RowMark shows that mark right-aligned at Dimension::IconSmall
// instead. The row text has no status words.
class ColumnRowDelegate : public QStyledItemDelegate {
  public:
    explicit ColumnRowDelegate(int detailRole, QObject* parent = nullptr, int markRole = -1);
    void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const override;

  private:
    int detailRole_;
    int markRole_;
};
} // namespace choscordb::design
