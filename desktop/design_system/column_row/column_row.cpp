#include "design_system/column_row/column_row.h"

#include "design_system/fonts/fonts.h"
#include "design_system/metrics/metrics.h"
#include "design_system/theme.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

namespace choscordb::design {
ColumnRowDelegate::ColumnRowDelegate(int detailRole, QObject* parent)
    : QStyledItemDelegate(parent), detailRole_(detailRole) {}

void ColumnRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    if (!option.widget) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    QStyleOptionViewItem item(option);
    initStyleOption(&item, index);
    const QRect textRect =
        option.widget->style()->subElementRect(QStyle::SE_ItemViewItemText, &item, option.widget);
    const QString name = item.text;
    const QString detail = index.data(detailRole_).toString();
    item.text.clear();
    item.icon = QIcon();
    item.features &= ~(QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration);
    option.widget->style()->drawControl(QStyle::CE_ItemViewItem, &item, painter, option.widget);

    const auto colors = resolvedThemeForWidget(*option.widget).colors;
    const auto nameFont = resolveTypography(TypographyRole::Ui);
    const auto detailFont = resolveTypography(TypographyRole::NavigationDetail);
    const QFontMetrics nameMetrics(nameFont);
    const QFontMetrics detailMetrics(detailFont);
    const int gap = detail.isEmpty() ? 0 : spacing(Spacing::One);
    const int available = qMax(0, textRect.width());
    const int detailWidth = detail.isEmpty() ? 0
                                             : qMin(detailMetrics.horizontalAdvance(detail),
                                                    qMax(0, (available - gap) / 2));
    const int nameWidth = qMax(0, available - gap - detailWidth);

    painter->save();
    painter->setClipRect(textRect);
    painter->setFont(nameFont);
    painter->setPen(colors.text);
    painter->drawText(QRect(textRect.left(), textRect.top(), nameWidth, textRect.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      nameMetrics.elidedText(name, Qt::ElideRight, nameWidth));
    if (detailWidth > 0) {
        painter->setFont(detailFont);
        painter->setPen(colors.mutedText);
        painter->drawText(QRect(textRect.right() - detailWidth + 1, textRect.top(), detailWidth,
                                textRect.height()),
                          Qt::AlignRight | Qt::AlignVCenter,
                          detailMetrics.elidedText(detail, Qt::ElideRight, detailWidth));
    }
    painter->restore();
}
} // namespace choscordb::design
