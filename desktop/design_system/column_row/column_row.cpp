#include "design_system/column_row/column_row.h"

#include "design_system/fonts/fonts.h"
#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "design_system/theme.h"

#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

namespace choscordb::design {
namespace {
void paintMarkedRow(QPainter* painter, QStyleOptionViewItem item, RowMark mark) {
    const auto* widget = item.widget;
    auto* style = widget->style();
    const auto colors = resolvedThemeForWidget(*widget).colors;
    const bool failed = mark == RowMark::Failed;
    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &item, widget);
    if (failed) {
        painter->fillRect(item.rect, colors.dangerTint);
        item.palette.setColor(QPalette::Text, colors.textDanger);
        item.palette.setColor(QPalette::HighlightedText, colors.textDanger);
    }
    const int size = dimension(Dimension::IconSmall);
    const QRect markRect(textRect.right() - size + 1, textRect.center().y() - size / 2 + 1, size,
                         size);
    painter->save();
    painter->setClipRect(item.rect.adjusted(
        0, 0, markRect.left() - spacing(Spacing::One) - item.rect.right() - 1, 0));
    style->drawControl(QStyle::CE_ItemViewItem, &item, painter, widget);
    painter->restore();
    auto end = item;
    end.text.clear();
    end.icon = QIcon();
    end.features &= ~(QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration);
    end.rect.setLeft(markRect.left() - spacing(Spacing::One));
    style->drawControl(QStyle::CE_ItemViewItem, &end, painter, widget);
    const Icon icon = mark == RowMark::Loading ? Icon::Loader
                      : failed                 ? Icon::Error
                                               : Icon::Warning;
    themedIcon(icon, failed ? colors.textDanger : colors.textSubtle, size).paint(painter, markRect);
}

}

ColumnRowDelegate::ColumnRowDelegate(int detailRole, QObject* parent, int markRole)
    : QStyledItemDelegate(parent), detailRole_(detailRole), markRole_(markRole) {}

void ColumnRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    if (!option.widget) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    QStyleOptionViewItem item(option);
    initStyleOption(&item, index);
    if (const auto mark = markRole_ < 0 ? RowMark::None : RowMark(index.data(markRole_).toInt());
        mark != RowMark::None) {
        paintMarkedRow(painter, item, mark);
        return;
    }
    const QRect textRect =
        option.widget->style()->subElementRect(QStyle::SE_ItemViewItemText, &item, option.widget);
    const QString name = item.text;
    const QString detail = index.data(detailRole_).toString();
    item.text.clear();
    item.icon = QIcon();
    item.features &= ~(QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration);
    option.widget->style()->drawControl(QStyle::CE_ItemViewItem, &item, painter, option.widget);

    const auto colors = resolvedThemeForWidget(*option.widget).colors;
    const auto detailFont = resolveTypography(TextStyle::Callout);
    const auto nameFont = detailFont;
    const QFontMetrics nameMetrics(nameFont);
    const QFontMetrics detailMetrics(detailFont);
    const int available = qMax(0, textRect.width());
    const int gap = detail.isEmpty() ? 0 : spacing(Spacing::One);
    const int contentWidth = qMax(0, available - gap);
    const int nameReserve = detail.isEmpty()
                                ? contentWidth
                                : qMin(nameMetrics.horizontalAdvance(name), contentWidth / 2);
    const int detailWidth = detail.isEmpty() ? 0
                                             : qMin(detailMetrics.horizontalAdvance(detail),
                                                    contentWidth - nameReserve);
    const int nameWidth = contentWidth - detailWidth;

    painter->save();
    painter->setClipRect(textRect);
    const bool enabled = option.state.testFlag(QStyle::State_Enabled);
    painter->setFont(nameFont);
    painter->setPen(enabled ? colors.textDefault : colors.textInactive);
    painter->drawText(QRect(textRect.left(), textRect.top(), nameWidth, textRect.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      nameMetrics.horizontalAdvance(name) <= nameWidth
                          ? name
                          : nameMetrics.elidedText(name, Qt::ElideRight, nameWidth));
    if (detailWidth > 0) {
        const auto* view = qobject_cast<const QAbstractItemView*>(option.widget);
        const bool selected =
            (option.state & QStyle::State_Selected) ||
            (view && view->selectionModel() && view->selectionModel()->isSelected(index));
        painter->setFont(detailFont);
        painter->setPen(!enabled   ? colors.textInactive
                        : selected ? colors.textDefault
                                   : colors.textSubtle);
        painter->drawText(QRect(textRect.right() - detailWidth + 1, textRect.top(), detailWidth,
                                textRect.height()),
                          Qt::AlignRight | Qt::AlignVCenter,
                          detailMetrics.horizontalAdvance(detail) <= detailWidth
                              ? detail
                              : detailMetrics.elidedText(detail, Qt::ElideRight, detailWidth));
    }
    painter->restore();
}
}
