#pragma once

#include <QStyledItemDelegate>

namespace choscordb::design {
class NavigationProfileDelegate final : public QStyledItemDelegate {
  public:
    enum { DriverRole = Qt::UserRole + 72 };
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override;
    void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const override;
};
}
