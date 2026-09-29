#pragma once

#include <QStyledItemDelegate>

namespace choscordb::design {
// A single-line navigation row with an independent, right-aligned database type.
class ColumnRowDelegate : public QStyledItemDelegate {
  public:
    explicit ColumnRowDelegate(int detailRole, QObject* parent = nullptr);
    void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const override;

  private:
    int detailRole_;
};
} // namespace choscordb::design
