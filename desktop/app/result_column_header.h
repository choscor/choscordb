#pragma once

#include "design_system/fonts/fonts.h"
#include "design_system/icons.h"
#include "models/result_table_model.h"
#include <QHeaderView>
#include <QPainter>
#include <QStyleOptionHeader>
#include <algorithm>

namespace choscordb {
// Keeps the full model label for accessibility while giving its type less visual weight.
class ResultColumnHeader final : public QHeaderView {
  public:
    explicit ResultColumnHeader(QWidget* parent = nullptr) : QHeaderView(Qt::Horizontal, parent) {
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

  protected:
    QSize sectionSizeFromContents(int logicalIndex) const override {
        QSize size = QHeaderView::sectionSizeFromContents(logicalIndex);
        if (!model())
            return size;
        const QString name =
            model()
                ->headerData(logicalIndex, Qt::Horizontal, ResultTableModel::HeaderNameRole)
                .toString();
        const QString type =
            model()
                ->headerData(logicalIndex, Qt::Horizontal, ResultTableModel::HeaderTypeRole)
                .toString();
        const QFont small = design::resolveTypography(design::TypographyRole::Small);
        const int icon =
            model()->headerData(logicalIndex, Qt::Horizontal, ResultTableModel::HeaderKeyRole)
                    .toBool()
                ? 16
                : 0;
        size.setWidth(
            std::max(size.width(),
                     26 + icon + fontMetrics().horizontalAdvance(name) +
                         (type.isEmpty() ? 0 : 8 + QFontMetrics(small).horizontalAdvance(type))));
        return size;
    }

    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override {
        if (!model() || !rect.isValid())
            return;
        const QString name =
            model()
                ->headerData(logicalIndex, Qt::Horizontal, ResultTableModel::HeaderNameRole)
                .toString();
        const QString type =
            model()
                ->headerData(logicalIndex, Qt::Horizontal, ResultTableModel::HeaderTypeRole)
                .toString();
        const bool key =
            model()
                ->headerData(logicalIndex, Qt::Horizontal, ResultTableModel::HeaderKeyRole)
                .toBool();
        QStyleOptionHeader option;
        initStyleOption(&option);
        option.rect = rect;
        option.section = logicalIndex;
        option.text.clear();
        option.sortIndicator =
            isSortIndicatorShown() && sortIndicatorSection() == logicalIndex
                ? (sortIndicatorOrder() == Qt::AscendingOrder ? QStyleOptionHeader::SortUp
                                                              : QStyleOptionHeader::SortDown)
                : QStyleOptionHeader::None;
        style()->drawControl(QStyle::CE_Header, &option, painter, this);
        painter->save();
        painter->setClipRect(rect.adjusted(1, 0, -1, 0));
        const QColor text = palette().color(QPalette::WindowText);
        int x = rect.left() + 12;
        if (key) {
            const auto icon = design::themedIcon(design::Icon::Key, text, 12);
            icon.paint(painter, QRect(x, rect.center().y() - 6, 12, 12));
            x += 17;
        }
        painter->setPen(text);
        const int available = std::max(0, rect.right() - x - 12);
        const int nameWidth = std::min(fontMetrics().horizontalAdvance(name), available);
        const QString shownName = fontMetrics().elidedText(name, Qt::ElideRight, nameWidth);
        painter->drawText(QRect(x, rect.top(), nameWidth, rect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, shownName);
        x += fontMetrics().horizontalAdvance(shownName) + 7;
        if (!type.isEmpty()) {
            const QFont small = design::resolveTypography(design::TypographyRole::Small);
            painter->setFont(small);
            QColor secondary = text;
            secondary.setAlphaF(0.65);
            painter->setPen(secondary);
            const int typeWidth = std::max(0, rect.right() - x - 12);
            painter->drawText(QRect(x, rect.top(), typeWidth, rect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              QFontMetrics(small).elidedText(QStringLiteral("· ") + type,
                                                             Qt::ElideRight, typeWidth));
        }
        painter->restore();
    }
};
} // namespace choscordb
