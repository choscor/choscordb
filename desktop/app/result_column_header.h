#pragma once

#include "design_system/fonts/fonts.h"
#include "design_system/icons.h"
#include "models/result_table_model.h"
#include <QHeaderView>
#include <QPainter>
#include <QStyleOptionHeader>
#include <QTextLayout>
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
                ? 17
                : 0;
        size.setWidth(24 + icon + fontMetrics().horizontalAdvance(name) +
                      (type.isEmpty() ? 0
                                      : 7 + QFontMetrics(small).horizontalAdvance(
                                                QStringLiteral("· ") + type)));
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
        const QString label = type.isEmpty() ? name : name + QStringLiteral(" · ") + type;
        const QFont small = design::resolveTypography(design::TypographyRole::Small);
        const QFontMetrics primaryMetrics(font());
        const QFontMetrics secondaryMetrics(small);
        const auto textWidth = [&](const QString& candidate) {
            const int primaryLength = type.isEmpty()
                                          ? candidate.size()
                                          : std::min(int(name.size()), int(candidate.size()));
            return primaryMetrics.horizontalAdvance(candidate.left(primaryLength)) +
                   secondaryMetrics.horizontalAdvance(candidate.mid(primaryLength));
        };
        QString shown = label;
        if (textWidth(label) > available) {
            int lower = 0;
            int upper = label.size();
            while (lower < upper) {
                const int middle = (lower + upper + 1) / 2;
                if (textWidth(label.left(middle) + QChar(0x2026)) <= available)
                    lower = middle;
                else
                    upper = middle - 1;
            }
            shown = label.left(lower) + QChar(0x2026);
        }
        QTextLayout layout(shown, font());
        QTextOption textOption;
        textOption.setWrapMode(QTextOption::NoWrap);
        layout.setTextOption(textOption);
        if (!type.isEmpty() && shown.size() > name.size()) {
            QTextCharFormat secondaryFormat;
            secondaryFormat.setFont(small);
            QColor secondary = text;
            secondary.setAlphaF(0.65);
            secondaryFormat.setForeground(secondary);
            layout.setFormats(
                {{int(name.size()), int(shown.size() - name.size()), secondaryFormat}});
        }
        layout.beginLayout();
        QTextLine line = layout.createLine();
        line.setLineWidth(available);
        layout.endLayout();
        layout.draw(painter, QPointF(x, rect.top() + (rect.height() - line.height()) / 2));
        painter->restore();
    }
};
} // namespace choscordb
