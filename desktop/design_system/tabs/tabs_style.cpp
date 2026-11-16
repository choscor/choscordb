#include "design_system/tabs/tabs_style.h"
#include "design_system/fonts/fonts.h"
#include "design_system/theme.h"
#include <QFocusEvent>
#include <QPainter>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QToolButton>

namespace choscordb::design {
DocumentTabBar::DocumentTabBar(QWidget* parent) : QTabBar(parent) {
    auto font = resolveTypography(TextStyle::Body);
    font.setWeight(QFont::Medium);
    setFont(font);
}

void DocumentTabBar::setTabTag(int index, std::optional<StatusTone> tone, const QString& label) {
    const QVariant value =
        tone ? QVariant(QVariantMap{{"tone", static_cast<int>(*tone)}, {"label", label}})
             : QVariant();
    if (tabData(index) == value)
        return;
    setTabData(index, value);
    update(tabRect(index));
}

std::optional<StatusTone> DocumentTabBar::tabTone(int index) const {
    const auto tone = tabData(index).toMap().value("tone");
    if (!tone.isValid())
        return std::nullopt;
    return static_cast<StatusTone>(tone.toInt());
}

QString DocumentTabBar::tabTagLabel(int index) const {
    return tabData(index).toMap().value("label").toString();
}

namespace {
void drawDocumentTabLabel(const QStyleOptionTab& tab, QPainter* painter, const DocumentTabBar& bar,
                          int index) {
    painter->save();
    const auto colors = resolvedThemeForWidget(bar).colors;
    painter->setPen(!tab.state.testFlag(QStyle::State_Enabled) ? colors.textInactive
                    : tab.state.testFlag(QStyle::State_Selected) ||
                            tab.state.testFlag(QStyle::State_MouseOver)
                        ? colors.textDefault
                        : colors.textSubtle);
    const int iconSize = dimension(Dimension::Icon);
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
    if (const auto tone = bar.tabTone(index)) {
        const int marker = spacing(Spacing::Two);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(statusToneColor(colors, *tone));
        painter->drawEllipse(QRect(left, tab.rect.center().y() - marker / 2, marker, marker));
        painter->restore();
        left += marker + spacing(Spacing::One);
    }
    const auto* close = bar.tabButton(index, QTabBar::RightSide);
    const int rightInset =
        spacing(Spacing::Two) + (close ? close->width() + spacing(Spacing::One) : 0);
    const int textWidth = qMax(0, tab.rect.right() - rightInset - left + 1);
    const auto metrics = painter->fontMetrics();
    const int baseline = tab.rect.top() + (tab.rect.height() + metrics.capHeight()) / 2;
    const QString unsavedMark = QStringLiteral(" \u2022");
    const bool unsaved = tab.text.endsWith(unsavedMark);
    const int markWidth = unsaved ? metrics.horizontalAdvance(unsavedMark) : 0;
    const auto title = metrics.elidedText(unsaved ? tab.text.chopped(unsavedMark.size()) : tab.text,
                                          bar.elideMode(), qMax(0, textWidth - markWidth));
    painter->drawText(QPoint(left, baseline), title);
    if (unsaved) {
        painter->setPen(colors.brand);
        painter->drawText(QPoint(left + metrics.horizontalAdvance(title), baseline), unsavedMark);
    }
    painter->restore();
}
}

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
        const qreal ring = focusSpec().ringWidth;
        painter.setPen(QPen(resolvedThemeForWidget(*this).colors.brand, ring));
        painter.setBrush(Qt::NoBrush);
        const auto bounds = QRectF(tabRect(currentIndex())).adjusted(ring, ring, -ring, -ring);
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
}
