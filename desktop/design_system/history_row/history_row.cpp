#include "design_system/history_row/history_row.h"

#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "design_system/theme.h"

#include <QPainter>
#include <QStyleOptionViewItem>

namespace choscordb::design {
namespace {
Icon driverIcon(const QString& driver) {
    if (driver == "postgres")
        return Icon::PostgreSQL;
    if (driver == "sqlite")
        return Icon::SQLite;
    if (driver == "mysql")
        return Icon::MySQL;
    return Icon::Database;
}

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
} // namespace

QSize RecentHistoryRowDelegate::sizeHint(const QStyleOptionViewItem& option,
                                         const QModelIndex&) const {
    const int height = typographySpec(TypographyRole::Metadata).lineHeight +
                       2 * typographySpec(TypographyRole::NavigationDetail).lineHeight +
                       spacing(Spacing::Two) + spacing(Spacing::Half);
    return {option.rect.width(), height};
}

void RecentHistoryRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
    if (!option.widget)
        return;
    const auto colors = resolvedThemeForWidget(*option.widget).colors;
    const auto bounds = option.rect.adjusted(1, 1, -1, -1);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const int inset = spacing(Spacing::Two);
    const int lineGap = spacing(Spacing::Half);
    const int textWidth = qMax(0, bounds.width() - 2 * inset);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(selected ? colors.border : Qt::transparent);
    painter->setBrush((selected || hovered) ? colors.muted : colors.sidebar);
    painter->drawRoundedRect(bounds, radius(Radius::Small), radius(Radius::Small));

    const QFont sqlFont = resolveTypography(TypographyRole::Metadata);
    const QFontMetrics sqlMetrics(sqlFont);
    auto sql = index.data(SqlRole).toString();
    sql.replace(QLatin1Char('\n'), QLatin1Char(' '));
    painter->setFont(sqlFont);
    painter->setPen(colors.text);
    const int firstY = bounds.top() + spacing(Spacing::One);
    const int sqlLineHeight = sqlMetrics.lineSpacing();
    const auto shown = sqlMetrics.elidedText(sql, Qt::ElideRight, textWidth);
    painter->drawText(QRect(bounds.left() + inset, firstY, textWidth, sqlLineHeight),
                      Qt::AlignLeft | Qt::AlignVCenter, shown);

    const QFont detailFont = resolveTypography(TypographyRole::NavigationDetail);
    const QFontMetrics detailMetrics(detailFont);
    painter->setFont(detailFont);
    const int dateY = bounds.bottom() - spacing(Spacing::One) - detailMetrics.height();
    const int connectionY = dateY - detailMetrics.height() - lineGap;
    const QString status = statusLabel(index.data(StatusRole).toString());
    const int badgeWidth = detailMetrics.horizontalAdvance(status) + 2 * inset;
    const int badgeLeft = bounds.right() - inset - badgeWidth;
    const QRect badge(badgeLeft, dateY - lineGap, badgeWidth, detailMetrics.height() + 2 * lineGap);
    QColor badgeInk = colors.mutedText;
    QColor badgeSurface = colors.muted;
    const auto rawStatus = index.data(StatusRole).toString();
    if (rawStatus == "completed") {
        badgeInk = colors.success;
        badgeSurface = colors.successSurface;
    } else if (rawStatus == "failed" || rawStatus == "disconnected") {
        badgeInk = colors.danger;
        badgeSurface = colors.dangerSurface;
    } else if (rawStatus == "cancelled") {
        badgeInk = colors.warning;
        badgeSurface = colors.warningSurface;
    }
    painter->setPen(Qt::NoPen);
    painter->setBrush(badgeSurface);
    painter->drawRoundedRect(badge, radius(Radius::Small), radius(Radius::Small));
    painter->setPen(badgeInk);
    painter->drawText(badge, Qt::AlignCenter, status);

    const int iconSize = dimension(Dimension::IconSmall);
    const int metadataLeft = bounds.left() + inset;
    themedIcon(driverIcon(index.data(DriverRole).toString()), colors.mutedText, iconSize)
        .paint(painter, QRect(metadataLeft, connectionY, iconSize, iconSize));
    const int connectionLeft = metadataLeft + iconSize + lineGap;
    const auto when = index.data(WhenRole).toString();
    const int connectionWidth = qMax(0, bounds.right() - inset - connectionLeft);
    painter->setPen(colors.mutedText);
    painter->drawText(QRect(connectionLeft, connectionY, connectionWidth, detailMetrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      detailMetrics.elidedText(index.data(ConnectionRole).toString(),
                                               Qt::ElideRight, connectionWidth));
    const int dateWidth = qMax(0, badgeLeft - lineGap - metadataLeft);
    painter->drawText(QRect(metadataLeft, dateY, dateWidth, detailMetrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      detailMetrics.elidedText(when, Qt::ElideRight, dateWidth));
    if (option.state & QStyle::State_HasFocus) {
        painter->setPen(QPen(colors.focus, focusSpec().borderWidth));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(bounds.adjusted(1, 1, -1, -1), radius(Radius::Small),
                                 radius(Radius::Small));
    }
    painter->restore();
}
} // namespace choscordb::design
