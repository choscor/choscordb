#include "history_model.h"
#include <QDateTime>
namespace choscordb {
HistoryModel::HistoryModel(QObject* parent) : QAbstractTableModel(parent) {}
int HistoryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : entries_.size();
}
int HistoryModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 6;
}
const SavedHistoryEntry* HistoryModel::entry(int row) const {
    return row >= 0 && row < entries_.size() ? &entries_[row] : nullptr;
}
void HistoryModel::setEntries(QList<SavedHistoryEntry> entries) {
    beginResetModel();
    entries_ = std::move(entries);
    endResetModel();
}
void HistoryModel::setProfileNames(QHash<QString, QString> names) {
    profileNames_ = std::move(names);
    if (!entries_.isEmpty())
        emit dataChanged(index(0, 1), index(entries_.size() - 1, 1));
}
QVariant HistoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
    static const QStringList labels = {tr("Timestamp"),   tr("Connection profile"),
                                       tr("SQL excerpt"), tr("Duration"),
                                       tr("Status"),      tr("Rows")};
    return orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 &&
                   section < labels.size()
               ? labels[section]
               : QVariant{};
}
QVariant HistoryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid())
        return {};
    const auto* value = entry(index.row());
    if (!value)
        return {};
    if (role == Qt::AccessibleTextRole && index.column() == 2) {
        QStringList parts;
        // Compact history hides the other visual columns; retain their real
        // metadata in the spoken row without exposing the unbounded SQL body.
        for (const int column : {2, 4, 0, 1, 3})
            parts.append(data(index.siblingAtColumn(column), Qt::DisplayRole).toString().left(256));
        parts.append(tr("%1 rows").arg(data(index.siblingAtColumn(5), Qt::DisplayRole).toString()));
        return parts.join(QStringLiteral(". "));
    }
    if (role == Qt::TextAlignmentRole && (index.column() == 3 || index.column() == 5))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};
    switch (index.column()) {
    case 0:
        return QDateTime::fromSecsSinceEpoch(value->timestamp).toLocalTime().toString(Qt::ISODate);
    case 1:
        return value->profileId.isEmpty() ? tr("Unsaved connection")
                                          : profileNames_.value(value->profileId, value->profileId);
    case 2: {
        // Bound the copy before whitespace normalization, even for a large SQL buffer.
        QString excerpt = value->sql.left(240);
        if (!excerpt.isEmpty() && excerpt.back().isHighSurrogate())
            excerpt.chop(1);
        excerpt = excerpt.simplified();
        if (value->sql.size() > 240)
            excerpt += QChar(0x2026);
        return excerpt;
    }
    case 3:
        return tr("%1 ms").arg(value->durationMs);
    case 4:
        if (value->status == "completed")
            return tr("Completed");
        if (value->status == "failed")
            return tr("Failed");
        if (value->status == "cancelled")
            return tr("Cancelled");
        if (value->status == "disconnected")
            return tr("Disconnected");
        return value->status;
    case 5:
        return value->hasRowCount ? QString::number(value->rowCount) : QString(QChar(0x2014));
    default:
        return {};
    }
}
} // namespace choscordb
