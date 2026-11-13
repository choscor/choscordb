#pragma once

#include "design_system/colors/colors.h"

#include <QStyledItemDelegate>

namespace choscordb::design {
struct StatusTint final {
    QColor ink;
    QColor surface;
};
[[nodiscard]] StatusTint historyStatusTint(const Colors& colors, QStringView status);

class RecentHistoryRowDelegate final : public QStyledItemDelegate {
  public:
    enum { SqlRole = Qt::UserRole + 72, ConnectionRole, WhenRole, StatusRole, DriverRole };
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override;
    void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const override;
};
}
