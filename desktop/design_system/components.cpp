#include "design_system/components.h"
#include "design_system/theme.h"
#include <QBoxLayout>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionButton>
#include <QtMath>

namespace choscordb::design {
namespace {
QFont buttonFont(ButtonSize size) {
    auto font = resolveTypography(TypographyRole::Ui);
    const int index = static_cast<int>(size) % 4;
    font.setPixelSize(index == 0 ? 12 : index == 1 ? 13 : 14);
    font.setWeight(QFont::Medium);
    return font;
}
} // namespace

Button::Button(const QString& text, QWidget* parent) : QPushButton(text, parent) {
    setProperty("designButton", true);
    setButtonSize(ButtonSize::Default);
}
void Button::setDesignIcon(Icon role) {
    designIcon_ = role;
    paintedIcon_ = {};
    setIcon(themedIcon(role, resolvedThemeForWidget(*this).colors.foreground, 16));
}
void Button::setVariant(ButtonVariant variant) {
    variant_ = variant;
    update();
}
ButtonVariant Button::variant() const {
    return variant_;
}
void Button::setButtonSize(ButtonSize size) {
    size_ = size;
    setFont(buttonFont(size_));
    const auto hint = sizeHint();
    setFixedHeight(hint.height());
    if (size_ >= ButtonSize::IconExtraSmall)
        setFixedWidth(hint.width());
    else {
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
    }
    updateGeometry();
}
ButtonSize Button::buttonSize() const {
    return size_;
}
void Button::setLoading(bool loading) {
    if (loading_ == loading)
        return;
    loading_ = loading;
    if (loading) {
        enabledBeforeLoading_ = isEnabled();
        setEnabled(false);
    } else
        setEnabled(enabledBeforeLoading_);
    setAccessibleDescription(loading ? tr("Loading") : QString{});
    updateGeometry();
    update();
}
bool Button::isLoading() const {
    return loading_;
}
QSize Button::sizeHint() const {
    const int index = static_cast<int>(size_) % 4;
    const int heights[] = {24, 28, 32, 36};
    const int height = heights[index];
    if (size_ >= ButtonSize::IconExtraSmall)
        return {height, height};
    const int padding = index == 0 ? 8 : 10;
    return {qCeil(QFontMetricsF(buttonFont(size_)).horizontalAdvance(text())) + 2 * padding + 2 +
                (!loading_ && icon().isNull() ? 0
                                              : (index == 0   ? 12
                                                 : index == 1 ? 14
                                                              : 16) +
                                                    (index < 2 ? 4 : 6)),
            height};
}
QStyle::State Button::visualState() const {
    QStyleOptionButton option;
    initStyleOption(&option);
    if (!keyboardFocus_)
        option.state &= ~QStyle::State_HasFocus;
    return option.state;
}
void Button::focusInEvent(QFocusEvent* event) {
    keyboardFocus_ = event->reason() != Qt::MouseFocusReason;
    QPushButton::focusInEvent(event);
    update();
}
void Button::paintEvent(QPaintEvent*) {
    const auto state = visualState();
    const auto theme = resolvedThemeForWidget(*this);
    const bool dark = theme.appearance == ResolvedAppearance::Dark;
    const auto& colors = theme.colors;
    const bool hover = state.testFlag(QStyle::State_MouseOver);
    QColor background = Qt::transparent;
    QColor foreground = colors.foreground;
    QColor border = Qt::transparent;
    auto alpha = [](QColor color, qreal opacity) {
        color.setAlphaF(color.alphaF() * opacity);
        return color;
    };
    switch (variant_) {
    case ButtonVariant::Default:
        background = hover ? alpha(colors.primary, .8) : colors.primary;
        foreground = colors.primaryForeground;
        break;
    case ButtonVariant::Secondary:
        background = colors.secondary;
        foreground = colors.secondaryForeground;
        if (hover)
            background = dark ? QColor("#2f2f2f") : QColor("#e7e7e7");
        break;
    case ButtonVariant::Outline:
        background = dark ? alpha(colors.input, hover ? .5 : .3)
                          : (hover ? colors.muted : colors.background);
        border = dark ? colors.input : colors.border;
        break;
    case ButtonVariant::Ghost:
        if (hover || state.testFlag(QStyle::State_On))
            background = alpha(colors.muted, dark ? .5 : 1.);
        break;
    case ButtonVariant::Destructive:
        background = alpha(colors.destructive, (dark ? .2 : .1) + (hover ? .1 : 0.));
        foreground = colors.danger;
        break;
    case ButtonVariant::Link:
        foreground = colors.primary;
        break;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!state.testFlag(QStyle::State_Enabled))
        painter.setOpacity(.5);
    const bool focus = state.testFlag(QStyle::State_HasFocus);
    QRectF panel = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
    const int radius = static_cast<int>(size_) % 4 < 2 ? 8 : 10;
    if (state.testFlag(QStyle::State_Sunken) && !menu())
        painter.translate(0, 1);
    painter.setBrush(background);
    painter.setPen(QPen(border, 1));
    if (property("groupFirst").isValid()) {
        const bool first = property("groupFirst").toBool();
        const bool last = property("groupLast").toBool();
        const bool vertical = property("groupVertical").toBool();
        const qreal tl = first ? radius : 0;
        const qreal tr = (vertical ? first : last) ? radius : 0;
        const qreal bl = (vertical ? last : first) ? radius : 0;
        const qreal br = last ? radius : 0;
        QPainterPath path;
        path.moveTo(panel.left() + tl, panel.top());
        path.lineTo(panel.right() - tr, panel.top());
        path.quadTo(panel.topRight(), QPointF(panel.right(), panel.top() + tr));
        path.lineTo(panel.right(), panel.bottom() - br);
        path.quadTo(panel.bottomRight(), QPointF(panel.right() - br, panel.bottom()));
        path.lineTo(panel.left() + bl, panel.bottom());
        path.quadTo(panel.bottomLeft(), QPointF(panel.left(), panel.bottom() - bl));
        path.lineTo(panel.left(), panel.top() + tl);
        path.quadTo(panel.topLeft(), QPointF(panel.left() + tl, panel.top()));
        path.closeSubpath();
        painter.drawPath(path);
    } else
        painter.drawRoundedRect(panel, radius, radius);
    if (focus) {
        painter.setPen(QPen(colors.focus, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(panel.adjusted(1, 1, -1, -1), radius, radius);
    }
    auto textFont = buttonFont(size_);
    const int index = static_cast<int>(size_) % 4;
    textFont.setUnderline(variant_ == ButtonVariant::Link && hover);
    painter.setFont(textFont);
    painter.setPen(foreground);
    const bool iconOnly = size_ >= ButtonSize::IconExtraSmall;
    const int iconPixels = index == 0 ? 12 : index == 1 && !iconOnly ? 14 : 16;
    const int gap = index < 2 ? 4 : 6;
    const bool hasLeading = loading_ || !icon().isNull();
    const auto label = painter.fontMetrics().elidedText(
        text(), Qt::ElideRight,
        qMax(0, width() - (index == 0 ? 18 : 22) - (hasLeading ? iconPixels + gap : 0)));
    const int textWidth = iconOnly ? 0 : painter.fontMetrics().horizontalAdvance(label);
    const int contentWidth = textWidth + (hasLeading ? iconPixels + (textWidth ? gap : 0) : 0);
    int x = (width() - contentWidth) / 2;
    if (loading_) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(foreground, 1.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(
            QRectF(x + 1, (height() - iconPixels) / 2. + 1, iconPixels - 2, iconPixels - 2),
            30 * 16, 270 * 16);
        painter.setPen(foreground);
    } else if (!icon().isNull()) {
        if (designIcon_ && (paintedIcon_.isNull() || paintedIconColor_ != foreground)) {
            paintedIcon_ = themedIcon(*designIcon_, foreground, 16);
            paintedIconColor_ = foreground;
        }
        const auto renderedIcon = designIcon_ ? paintedIcon_ : icon();
        renderedIcon.paint(&painter, QRect(x, (height() - iconPixels) / 2, iconPixels, iconPixels),
                           Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
    }
    if (hasLeading)
        x += iconPixels + gap;
    if (!iconOnly)
        painter.drawText(QRect(x, 0, textWidth, height()), Qt::AlignCenter | Qt::TextShowMnemonic,
                         label);
}
ButtonGroup::ButtonGroup(Qt::Orientation orientation, QWidget* parent)
    : QWidget(parent),
      layout_(new QBoxLayout(orientation == Qt::Horizontal ? QBoxLayout::LeftToRight
                                                           : QBoxLayout::TopToBottom,
                             this)) {
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
}
void ButtonGroup::addButton(Button* button) {
    if (!button)
        return;
    layout_->addWidget(button);
    for (int i = 0; i < layout_->count(); ++i) {
        auto* item = layout_->itemAt(i)->widget();
        item->setProperty("groupFirst", i == 0);
        item->setProperty("groupLast", i == layout_->count() - 1);
        item->setProperty("groupVertical", layout_->direction() == QBoxLayout::TopToBottom);
        item->update();
    }
}
} // namespace choscordb::design
