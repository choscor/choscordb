#pragma once

#include "design_system/fonts/fonts.h"
#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "models/result_table_model.h"
#include <QEvent>
#include <QHash>
#include <QHeaderView>
#include <QPainter>
#include <QStyleOptionHeader>
#include <QTextLayout>
#include <algorithm>
#include <cstdlib>
#include <optional>

namespace choscordb {
class ResultColumnHeader final : public QHeaderView {
  public:
    explicit ResultColumnHeader(QWidget* parent = nullptr) : QHeaderView(Qt::Horizontal, parent) {
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

  protected:
    void changeEvent(QEvent* event) override {
        QHeaderView::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange ||
            event->type() == QEvent::PaletteChange)
            clearPaintCache();
    }

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
        const int icon =
            model()->headerData(logicalIndex, Qt::Horizontal, ResultTableModel::HeaderKeyRole)
                    .toBool()
                ? iconAdvance()
                : 0;
        size.setWidth(
            2 * inset() + icon + (sectionsClickable() ? iconAdvance() : 0) +
            fontMetrics().horizontalAdvance(name) +
            (type.isEmpty()
                 ? 0
                 : QFontMetrics(secondaryFont()).horizontalAdvance(QStringLiteral(" · ") + type)));
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
        initStyleOptionForIndex(&option, logicalIndex);
        option.rect = rect;
        option.text.clear();
        option.icon = {};
        const int sortRank =
            model()
                ->headerData(logicalIndex, Qt::Horizontal, ResultTableModel::HeaderSortRole)
                .toInt();
        const bool sorted = sortRank != 0;
        const auto sortOrder = sortRank > 0 ? Qt::AscendingOrder : Qt::DescendingOrder;
        option.sortIndicator = QStyleOptionHeader::None;
        style()->drawControl(QStyle::CE_Header, &option, painter, this);
        painter->save();
        painter->setClipRect(rect.adjusted(1, 0, -1, 0));
        const QColor text = palette().color(QPalette::WindowText);
        int x = rect.left() + inset();
        if (key) {
            const int iconSize = design::dimension(design::Dimension::IconSmall);
            if (!keyIcon_ || keyIconColor_ != text.rgba()) {
                keyIcon_ = design::themedIcon(design::Icon::Key, text, iconSize);
                keyIconColor_ = text.rgba();
            }
            keyIcon_->paint(painter,
                            QRect(x, rect.center().y() - iconSize / 2, iconSize, iconSize));
            x += iconAdvance();
        }
        const int markSize = design::dimension(design::Dimension::IconSmall);
        if (sorted) {
            if (!sortIcon_ || sortIconColor_ != text.rgba() || sortIconOrder_ != sortOrder) {
                sortIcon_ =
                    design::themedIcon(sortOrder == Qt::AscendingOrder ? design::Icon::ArrowUp
                                                                       : design::Icon::ArrowDown,
                                       text, markSize);
                sortIconColor_ = text.rgba();
                sortIconOrder_ = sortOrder;
            }
            const QRect mark(rect.right() + 1 - inset() - markSize,
                             rect.center().y() - markSize / 2, markSize, markSize);
            sortIcon_->paint(painter, mark);
            if (std::abs(sortRank) > 1) {
                painter->setFont(secondaryFont());
                painter->setPen(text);
                painter->drawText(
                    QRect(mark.left() - iconAdvance(), rect.top(), iconAdvance(), rect.height()),
                    Qt::AlignRight | Qt::AlignVCenter, QString::number(std::abs(sortRank)));
                painter->setFont(font());
            }
        }
        painter->setPen(text);
        const int available =
            std::max(0, rect.left() + rect.width() - x - inset() -
                            (sorted ? iconAdvance() * (std::abs(sortRank) > 1 ? 2 : 1) : 0));
        const QString label = type.isEmpty() ? name : name + QStringLiteral(" · ") + type;
        const QFont& small = secondaryFont();
        auto& elided = elided_[logicalIndex];
        if (elided.label != label || elided.available != available) {
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
            elided = {label, available, shown};
        }
        const QString& shown = elided.shown;
        QTextLayout layout(shown, font());
        QTextOption textOption;
        textOption.setWrapMode(QTextOption::NoWrap);
        layout.setTextOption(textOption);
        if (!type.isEmpty() && shown.size() > name.size()) {
            QTextCharFormat secondaryFormat;
            secondaryFormat.setFont(small);
            QColor secondary = text;
            secondary.setAlphaF(0.65f);
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

  private:
    struct ElidedLabel {
        QString label;
        int available = -1;
        QString shown;
    };
    static int inset() { return design::spacing(design::Spacing::Three); }
    static int iconAdvance() {
        return design::dimension(design::Dimension::IconSmall) +
               design::spacing(design::Spacing::OneHalf);
    }
    const QFont& secondaryFont() const {
        if (!secondaryFont_)
            secondaryFont_ = design::resolveTypography(design::TextStyle::Callout);
        return *secondaryFont_;
    }
    void clearPaintCache() {
        secondaryFont_.reset();
        keyIcon_.reset();
        sortIcon_.reset();
        elided_.clear();
    }
    mutable std::optional<QFont> secondaryFont_;
    mutable std::optional<QIcon> keyIcon_;
    mutable QRgb keyIconColor_ = 0;
    mutable QHash<int, ElidedLabel> elided_;
    mutable std::optional<QIcon> sortIcon_;
    mutable QRgb sortIconColor_ = 0;
    mutable Qt::SortOrder sortIconOrder_ = Qt::AscendingOrder;
};
}
