#pragma once

#include "design_system/colors/colors.h"

#include <QStyledItemDelegate>

namespace choscordb::design {
// Status badges use the status surface with status text; any other status is
// neutral. `status` is the untranslated protocol status ("completed", ...).
struct StatusTint final {
    QColor ink;
    QColor surface;
};
[[nodiscard]] StatusTint historyStatusTint(const Colors& colors, QStringView status);

// Shared presentation for a recent query in a QListWidget. The application
// supplies formatted SQL and metadata; DisplayRole remains plain searchable
// text for Qt's accessibility and item matching.
class RecentHistoryRowDelegate final : public QStyledItemDelegate {
  public:
    enum { SqlRole = Qt::UserRole + 72, ConnectionRole, WhenRole, StatusRole, DriverRole };
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override;
    void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const override;
};
} // namespace choscordb::design
