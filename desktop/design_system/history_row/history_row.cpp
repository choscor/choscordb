#include "design_system/history_row/history_row.h"
#include "design_system/field/focus_indicator.h"

#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "design_system/theme.h"

#include <QPainter>
#include <QStyleOptionViewItem>

namespace choscordb::design {
StatusTint historyStatusTint(const Colors& colors, QStringView status) {
    if (status == u"completed")
        return {colors.textSuccess, colors.successTint};
    if (status == u"failed" || status == u"disconnected")
        return {colors.textDanger, colors.dangerTint};
    if (status == u"cancelled")
        return {colors.textWarning, colors.warningTint};
    return {colors.textSubtle, colors.elevated};
}

namespace {
QString statusLabel(const QString& status) {
    if (status == "completed")
        return RecentHistoryRowDelegate::tr("Completed");
    if (status == "failed")
        return RecentHistoryRowDelegate::tr("Failed");
    if (status == "cancelled")
        return RecentHistoryRowDelegate::tr("Cancelled");
    if (status == "disconnected")
        return RecentHistoryRowDelegate::tr("Disconnected");
    return status;
}
}

QSize RecentHistoryRowDelegate::sizeHint(const QStyleOptionViewItem& option,
                                         const QModelIndex&) const {
    return {option.rect.width(), 2 * dimension(Dimension::Row)};
}

void RecentHistoryRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
    if (!option.widget)
        return;
    const auto colors = resolvedThemeForWidget(*option.widget).colors;
    const auto bounds = option.rect.adjusted(spacing(Spacing::One), 0, -spacing(Spacing::One), 0);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool enabled = option.state & QStyle::State_Enabled;
    const int inset = spacing(Spacing::Three);
    const int row = dimension(Dimension::Row);
    const QRect firstLine(bounds.left() + inset, bounds.top(), bounds.width() - 2 * inset, row);
    const QRect secondLine(firstLine.left(), bounds.top() + row - spacing(Spacing::One),
                           firstLine.width(), row);
    const auto ink = enabled ? colors.textDefault : colors.textInactive;
    const auto muted = enabled ? colors.textSubtle : colors.textInactive;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const qreal gap = layoutMetrics().navigationHighlightGap;
    const auto highlight = QRectF(bounds).adjusted(0, gap / 2, 0, -gap / 2);
    if (selected || hovered) {
        const auto base = selected ? colors.tint : colors.elevated;
        painter->setPen(Qt::NoPen);
        painter->setBrush(hovered ? composite(colors.tint, base) : base);
        painter->drawRoundedRect(highlight, radius(Radius::Large), radius(Radius::Large));
    }

    const QFont detailFont = resolveTypography(TextStyle::Callout);
    const QFontMetrics detailMetrics(detailFont);
    const QFont badgeFont = resolveTypography(TextStyle::Caption);
    const QFontMetrics badgeMetrics(badgeFont);
    const QString status = statusLabel(index.data(StatusRole).toString());
    const int badgeWidth = badgeMetrics.horizontalAdvance(status) + 2 * spacing(Spacing::Two);
    const int badgeHeight = dimension(Dimension::Badge);
    const QRect badge(firstLine.right() - badgeWidth + 1,
                      firstLine.center().y() - badgeHeight / 2 + 1, badgeWidth, badgeHeight);
    const auto tint = historyStatusTint(colors, index.data(StatusRole).toString());
    painter->setFont(badgeFont);
    painter->setPen(Qt::NoPen);
    painter->setBrush(enabled ? tint.surface : colors.elevated);
    painter->drawRoundedRect(badge, badgeHeight / 2.0, badgeHeight / 2.0);
    painter->setPen(enabled ? tint.ink : colors.textInactive);
    painter->drawText(badge, Qt::AlignCenter, status);

    const QFont sqlFont = resolveTypography(TextStyle::Callout, FontDesign::Monospaced);
    const QFontMetrics sqlMetrics(sqlFont);
    auto sql = index.data(SqlRole).toString();
    sql.replace(QLatin1Char('\n'), QLatin1Char(' '));
    const int sqlWidth = qMax(0, badge.left() - inset - firstLine.left());
    painter->setFont(sqlFont);
    painter->setPen(ink);
    painter->drawText(QRect(firstLine.left(), firstLine.top(), sqlWidth, row),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      sqlMetrics.elidedText(sql, Qt::ElideRight, sqlWidth));

    const int iconSize = dimension(Dimension::IconSmall);
    themedIcon(driverIcon(index.data(DriverRole).toString()), muted, iconSize)
        .paint(painter, QRect(secondLine.left(), secondLine.center().y() - iconSize / 2 + 1,
                              iconSize, iconSize));
    const int metadataLeft = secondLine.left() + iconSize + spacing(Spacing::OneHalf);
    const int metadataWidth = qMax(0, secondLine.right() - metadataLeft);
    const auto metadata =
        tr("%1 · %2").arg(index.data(ConnectionRole).toString(), index.data(WhenRole).toString());
    painter->setFont(detailFont);
    painter->setPen(selected && enabled ? colors.textDefault : muted);
    painter->drawText(QRect(metadataLeft, secondLine.top(), metadataWidth, row),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      detailMetrics.elidedText(metadata, Qt::ElideRight, metadataWidth));
    paintRowFocusRing(*painter, option, option.widget, highlight, radius(Radius::Large));
    painter->restore();
}
}
