#include "design_system/tabs/tabs_style.h"
#include "design_system/fonts/fonts.h"
#include "design_system/style/style_resource.h"
#include "design_system/theme.h"
#include <QFocusEvent>
#include <QPainter>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QToolButton>

namespace choscordb::design {
DocumentTabBar::DocumentTabBar(QWidget* parent) : QTabBar(parent) {
    setFont(resolveTypography(TypographyRole::Small));
}

QString tabsStyleSheet() {
    return loadStyleSheet(QStringLiteral("tabs/tabs_style_sheet.qss"));
}

QString tabsApplicationStyleSheet() {
    return loadStyleSheet(QStringLiteral("tabs/tabs_application_style_sheet.qss"));
}
namespace {
void drawDocumentTabLabel(const QStyleOptionTab& tab, QPainter* painter, const QTabBar& bar,
                          int index) {
    painter->save();
    auto font = painter->font();
    font.setBold(tab.state.testFlag(QStyle::State_Selected));
    painter->setFont(font);
    const auto colors = resolvedThemeForWidget(bar).colors;
    painter->setPen(!tab.state.testFlag(QStyle::State_Enabled) ? colors.disabled
                    : tab.state.testFlag(QStyle::State_Selected) ||
                            tab.state.testFlag(QStyle::State_MouseOver)
                        ? colors.foreground
                        : colors.mutedText);
    const int iconSize = dimension(Dimension::IconSmall);
    int left = tab.rect.left() + spacing(Spacing::Two);
    if (const auto* leading = bar.tabButton(index, QTabBar::LeftSide))
        left += leading->width() + spacing(Spacing::One);
    if (!tab.icon.isNull()) {
        const QRect iconRect(left, tab.rect.center().y() - (iconSize - 1) / 2, iconSize, iconSize);
        tab.icon.paint(painter, iconRect, Qt::AlignCenter,
                       tab.state.testFlag(QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled,
                       tab.state.testFlag(QStyle::State_Selected) ? QIcon::On : QIcon::Off);
        left += iconSize + spacing(Spacing::One);
    }
    const auto* close = bar.tabButton(index, QTabBar::RightSide);
    const int rightInset =
        spacing(Spacing::Two) + (close ? close->width() + spacing(Spacing::One) : 0);
    const QRect textRect(left, tab.rect.top(), qMax(0, tab.rect.right() - rightInset - left + 1),
                         tab.rect.height());
    painter->drawText(
        textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
        painter->fontMetrics().elidedText(tab.text, bar.elideMode(), textRect.width()));
    painter->restore();
}
} // namespace

void DocumentTabBar::paintEvent(QPaintEvent* event) {
    if (property("designTabVariant").toString() != QLatin1String("document")) {
        QTabBar::paintEvent(event);
        return;
    }
    QStylePainter painter(this);
    QRect visibleTabs = rect();
    for (auto* button : findChildren<QToolButton*>(QString{}, Qt::FindDirectChildrenOnly)) {
        if (!button->isVisible())
            continue;
        bool tabButton = false;
        for (int index = 0; index < count(); ++index) {
            tabButton |= button == this->tabButton(index, QTabBar::LeftSide) ||
                         button == this->tabButton(index, QTabBar::RightSide);
        }
        if (tabButton)
            continue;
        if (button->geometry().center().x() < width() / 2)
            visibleTabs.setLeft(qMax(visibleTabs.left(), button->geometry().right() + 1));
        else
            visibleTabs.setRight(qMin(visibleTabs.right(), button->geometry().left() - 1));
    }
    painter.setClipRect(visibleTabs);
    const auto drawTab = [this, &painter](int index) {
        QStyleOptionTab option;
        initStyleOption(&option, index);
        painter.drawControl(QStyle::CE_TabBarTabShape, option);
        drawDocumentTabLabel(option, &painter, *this, index);
    };
    for (int index = 0; index < count(); ++index) {
        if (index != currentIndex())
            drawTab(index);
    }
    if (currentIndex() >= 0)
        drawTab(currentIndex());
    if (keyboardFocus_ && hasFocus() && currentIndex() >= 0) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(resolvedThemeForWidget(*this).colors.focus, focusSpec().ringWidth));
        painter.setBrush(Qt::NoBrush);
        const auto bounds = QRectF(tabRect(currentIndex())).adjusted(3, 3, -3, -3);
        painter.drawRoundedRect(bounds, radius(Radius::Small), radius(Radius::Small));
        painter.restore();
    }
}

void DocumentTabBar::focusInEvent(QFocusEvent* event) {
    keyboardFocus_ = event->reason() == Qt::TabFocusReason ||
                     event->reason() == Qt::BacktabFocusReason ||
                     event->reason() == Qt::ShortcutFocusReason;
    QTabBar::focusInEvent(event);
    update();
}

void DocumentTabBar::focusOutEvent(QFocusEvent* event) {
    keyboardFocus_ = false;
    QTabBar::focusOutEvent(event);
    update();
}
} // namespace choscordb::design
