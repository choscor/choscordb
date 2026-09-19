#pragma once

#include <QString>
#include <QStyledItemDelegate>

namespace choscordb::design {
// A bounded two-line sidebar row. DisplayRole contains title and detail
// separated by a newline, paired with a theme-aware database icon.
class NavigationProfileDelegate final : public QStyledItemDelegate {
  public:
    enum { DriverRole = Qt::UserRole + 72 };
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override;
    void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const override;
};
QString itemViewStyleSheet();
QString itemViewStateStyleSheet();
QString itemViewApplicationStyleSheet();
} // namespace choscordb::design
