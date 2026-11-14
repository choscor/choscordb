#include "design_system/button/button.h"
#include "design_system/theme.h"
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionButton>
#include <QVariantAnimation>
#include <QtMath>

namespace choscordb::design {
namespace {
bool onBanner(const QWidget& widget) {
    for (auto* ancestor = widget.parentWidget(); ancestor; ancestor = ancestor->parentWidget())
        if (ancestor->property("designSurface").toString() == QLatin1String("banner"))
            return true;
    return false;
}
QColor blendColor(const QColor& target, QColor base, qreal amount) {
    if (base.alpha() == 0) {
        base = target;
        base.setAlpha(0);
    }
    if (amount <= 0.0)
        return base;
    if (amount >= 1.0)
        return target;
    return QColor::fromRgbF(
        static_cast<float>(base.redF() + (target.redF() - base.redF()) * amount),
        static_cast<float>(base.greenF() + (target.greenF() - base.greenF()) * amount),
        static_cast<float>(base.blueF() + (target.blueF() - base.blueF()) * amount),
        static_cast<float>(base.alphaF() + (target.alphaF() - base.alphaF()) * amount));
}
int sizeIndex(ButtonSize size) {
    return static_cast<int>(size) % 3;
}
int buttonPadding(ButtonSize size) {
    const Spacing padding[] = {Spacing::OneHalf, Spacing::Two, Spacing::Three};
    return spacing(padding[sizeIndex(size)]);
}
int leadingIconPadding(ButtonSize size) {
    const Spacing padding[] = {Spacing::One, Spacing::OneHalf, Spacing::TwoHalf};
    return spacing(padding[sizeIndex(size)]);
}
int iconGap(ButtonSize size) {
    return spacing(sizeIndex(size) == 2 ? Spacing::OneHalf : Spacing::One);
}
int iconPixels(ButtonSize size) {
    return dimension(sizeIndex(size) == 2 ? Dimension::Icon : Dimension::IconSmall);
}
int panelRadius(ButtonSize size) {
    const Radius radii[] = {Radius::Small, Radius::Medium, Radius::Large};
    return radius(radii[sizeIndex(size)]);
}
QFont buttonFont(ButtonSize size) {
    auto font = resolveTypography(sizeIndex(size) == 2 ? TextStyle::Body : TextStyle::Caption);
    font.setWeight(QFont::Medium);
    return font;
}
QPainterPath panelPath(const QRectF& panel, qreal tl, qreal tr, qreal bl, qreal br) {
    QPainterPath path;
    path.moveTo(panel.left() + tl, panel.top());
    path.lineTo(panel.right() - tr, panel.top());
    path.arcTo(QRectF(panel.right() - 2 * tr, panel.top(), 2 * tr, 2 * tr), 90, -90);
    path.lineTo(panel.right(), panel.bottom() - br);
    path.arcTo(QRectF(panel.right() - 2 * br, panel.bottom() - 2 * br, 2 * br, 2 * br), 0, -90);
    path.lineTo(panel.left() + bl, panel.bottom());
    path.arcTo(QRectF(panel.left(), panel.bottom() - 2 * bl, 2 * bl, 2 * bl), 270, -90);
    path.lineTo(panel.left(), panel.top() + tl);
    path.arcTo(QRectF(panel.left(), panel.top(), 2 * tl, 2 * tl), 180, -90);
    path.closeSubpath();
    return path;
}
}

Button::Button(const QString& text, QWidget* parent) : QPushButton(text, parent) {
    setProperty("designButton", true);
    setButtonSize(ButtonSize::Default);
}
void Button::setButtonContext(ButtonContext context) {
    context_ = context;
    setButtonSize(size_);
    update();
}
ButtonContext Button::buttonContext() const {
    return context_;
}
void Button::setIcon(const QIcon& icon) {
    designIcon_.reset();
    paintedIcon_ = {};
    QPushButton::setIcon(icon);
    setButtonSize(size_);
}
void Button::setDesignIcon(Icon role) {
    designIcon_ = role;
    paintedIcon_ = {};
    QPushButton::setIcon(themedIcon(role, resolvedThemeForWidget(*this).colors.textDefault,
                                    dimension(Dimension::Icon)));
    setButtonSize(size_);
}
void Button::setVariant(ButtonVariant variant) {
    variant_ = variant;
    if (variant_ == ButtonVariant::Link)
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
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
    if (size_ >= ButtonSize::IconExtraSmall || context_ == ButtonContext::Choice)
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
    setButtonSize(size_);
    updateGeometry();
    update();
}
bool Button::isLoading() const {
    return loading_;
}
QSize Button::sizeHint() const {
    const int heights[] = {dimension(Dimension::ControlExtraSmall),
                           dimension(Dimension::ControlSmall), dimension(Dimension::Control)};
    if (context_ == ButtonContext::Choice) {
        const int side = dimension(Dimension::Logo) + 2 * spacing(Spacing::Three);
        return {side, side};
    }
    const int height = context_ == ButtonContext::Segment
                           ? dimension(Dimension::Tab) - 2 * spacing(Spacing::Half)
                           : heights[sizeIndex(size_)];
    if (size_ == ButtonSize::IconExtraSmall)
        return {dimension(Dimension::IconSmall), dimension(Dimension::IconSmall)};
    if (size_ >= ButtonSize::IconExtraSmall)
        return {height, height};
    const bool leading = loading_ || !icon().isNull();
    const int left = leading ? leadingIconPadding(size_) : buttonPadding(size_);
    return {qCeil(QFontMetricsF(buttonFont(size_)).horizontalAdvance(text())) + left +
                buttonPadding(size_) + 2 + (leading ? iconPixels(size_) + iconGap(size_) : 0),
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
    if (event->reason() == Qt::MouseFocusReason)
        keyboardFocus_ = false;
    else if (event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason ||
             event->reason() == Qt::ShortcutFocusReason)
        keyboardFocus_ = true;
    QPushButton::focusInEvent(event);
    update();
}
bool Button::event(QEvent* event) {
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
        animateHover(event->type() == QEvent::Enter ? 1.0 : 0.0);
    return QPushButton::event(event);
}
void Button::animateHover(qreal target) {
    const auto motion =
        motionSpec(Motion::Interaction, resolvedThemeForWidget(*this).reducedMotion);
    const auto duration = motion.durationMs;
    if (duration == 0) {
        if (hoverAnimation_)
            hoverAnimation_->stop();
        return;
    }
    if (!hoverAnimation_) {
        hoverAnimation_ = new QVariantAnimation(this);
        hoverAnimation_->setEasingCurve(motion.easing);
        connect(hoverAnimation_, &QVariantAnimation::valueChanged, this,
                qOverload<>(&QWidget::update));
    }
    const qreal start = hoverAnimation_->state() == QAbstractAnimation::Running
                            ? hoverAnimation_->currentValue().toReal()
                            : 1.0 - target;
    hoverAnimation_->stop();
    hoverAnimation_->setDuration(duration);
    hoverAnimation_->setStartValue(start);
    hoverAnimation_->setEndValue(target);
    hoverAnimation_->start();
}
qreal Button::hoverAmount(bool hover) const {
    if (hoverAnimation_ && hoverAnimation_->state() == QAbstractAnimation::Running)
        return hoverAnimation_->currentValue().toReal();
    return hover ? 1.0 : 0.0;
}
void Button::paintEvent(QPaintEvent*) {
    const auto state = visualState();
    const auto theme = resolvedThemeForWidget(*this);
    const auto& colors = theme.colors;
    const bool enabled = state.testFlag(QStyle::State_Enabled);
    const bool hover = enabled && state.testFlag(QStyle::State_MouseOver);
    const bool pressed = enabled && state.testFlag(QStyle::State_Sunken);
    const qreal amount = pressed ? 1.0 : hoverAmount(hover);
    const bool emphasized =
        variant_ == ButtonVariant::Default || variant_ == ButtonVariant::Destructive;
    const auto emphasis =
        emphasisFill(colors, theme.appearance, theme.forcedContrast,
                     variant_ == ButtonVariant::Destructive ? Emphasis::Danger : Emphasis::Brand);
    QColor rest = Qt::transparent;
    QColor hovered = onBanner(*this) ? colors.fill : colors.tint;
    QColor ring = Qt::transparent;
    QColor hoveredRing = Qt::transparent;
    QColor foreground = colors.textDefault;
    QColor activeForeground = colors.textDefault;
    switch (variant_) {
    case ButtonVariant::Default:
    case ButtonVariant::Destructive:
        ring = hoveredRing = emphasis.ring;
        foreground = activeForeground = colors.white;
        break;
    case ButtonVariant::Secondary:
        rest = colors.base;
        ring = hoveredRing = colors.line;
        break;
    case ButtonVariant::Outline:
        hovered = Qt::transparent;
        ring = colors.line;
        hoveredRing = withOpacity(colors.focus, 0.25);
        activeForeground = colors.textStrong;
        break;
    case ButtonVariant::Ghost:
        break;
    case ButtonVariant::Link:
        hovered = Qt::transparent;
        foreground = activeForeground = colors.brand;
        break;
    }
    if (context_ == ButtonContext::Segment) {
        hovered = Qt::transparent;
        foreground = colors.textSubtle;
    }
    if (isChecked() && !emphasized) {
        rest = hovered = context_ == ButtonContext::Segment ? colors.base : colors.tint;
        foreground = activeForeground = colors.textDefault;
    }
    if (context_ == ButtonContext::Segment && isChecked())
        ring = hoveredRing = colors.line;
    if (context_ == ButtonContext::Choice && isChecked())
        ring = hoveredRing = colors.brand;
    if (context_ == ButtonContext::TabAction) {
        hovered = Qt::transparent;
        activeForeground = colors.brand;
    }
    if (amount >= 0.5)
        foreground = activeForeground;
    if (!enabled && variant_ == ButtonVariant::Secondary)
        rest = withOpacity(colors.base, 0.5);
    const QColor background = enabled ? blendColor(hovered, rest, amount) : rest;
    const QColor border = enabled ? blendColor(hoveredRing, ring, amount) : ring;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!enabled)
        painter.setOpacity(0.5);
    const int radius =
        context_ == ButtonContext::Segment ? design::radius(Radius::Medium) : panelRadius(size_);
    const bool grouped = property("groupFirst").isValid();
    const bool first = !grouped || property("groupFirst").toBool();
    const bool last = !grouped || property("groupLast").toBool();
    const bool vertical = property("groupVertical").toBool();
    const qreal tl = first ? radius : 0;
    const qreal tr = (vertical ? first : last) ? radius : 0;
    const qreal bl = (vertical ? last : first) ? radius : 0;
    const qreal br = last ? radius : 0;
    const QRectF outer = rect();
    const QRectF inner =
        outer.adjusted(1, 1, last || vertical ? -1 : 0, last || !vertical ? -1 : 0);
    const auto innerPath = panelPath(inner, qMax<qreal>(0, tl - 1), qMax<qreal>(0, tr - 1),
                                     qMax<qreal>(0, bl - 1), qMax<qreal>(0, br - 1));
    painter.setPen(Qt::NoPen);
    painter.fillPath(panelPath(outer, tl, tr, bl, br).subtracted(innerPath), border);
    if (emphasized) {
        QLinearGradient gradient(inner.topLeft(), inner.bottomLeft());
        gradient.setColorAt(0, enabled
                                   ? blendColor(emphasis.background, emphasis.gradientStart, amount)
                                   : emphasis.gradientStart);
        gradient.setColorAt(1, emphasis.gradientEnd);
        painter.fillPath(innerPath, gradient);
        painter.save();
        painter.setClipPath(innerPath);
        painter.fillRect(QRectF(inner.left(), inner.top(), inner.width(), 1), emphasis.background);
        painter.restore();
    } else
        painter.fillPath(innerPath, background);
    if (state.testFlag(QStyle::State_HasFocus)) {
        const qreal width = focusSpec().ringWidth;
        painter.setPen(QPen(emphasized ? emphasis.ring : colors.brand, width));
        painter.setBrush(Qt::NoBrush);
        const qreal inset = width / 2;
        painter.drawPath(panelPath(outer.adjusted(inset, inset, -inset, -inset),
                                   qMax<qreal>(0, tl - inset), qMax<qreal>(0, tr - inset),
                                   qMax<qreal>(0, bl - inset), qMax<qreal>(0, br - inset)));
    }
    auto textFont = buttonFont(size_);
    textFont.setUnderline(variant_ == ButtonVariant::Link && hover);
    painter.setFont(textFont);
    painter.setPen(foreground);
    const bool iconOnly = size_ >= ButtonSize::IconExtraSmall;
    const int pixels =
        context_ == ButtonContext::Choice ? dimension(Dimension::Logo) : iconPixels(size_);
    const int gap = iconGap(size_);
    const bool hasLeading = loading_ || !icon().isNull();
    const int left = hasLeading ? leadingIconPadding(size_) : buttonPadding(size_);
    const auto label = painter.fontMetrics().elidedText(
        text(), Qt::ElideRight,
        qMax(0, width() - left - buttonPadding(size_) - 2 - (hasLeading ? pixels + gap : 0)));
    const int textWidth = iconOnly ? 0 : painter.fontMetrics().horizontalAdvance(label);
    const int contentWidth = textWidth + (hasLeading ? pixels + (textWidth ? gap : 0) : 0);
    int x = (width() - contentWidth) / 2;
    if (loading_) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(foreground, 1.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(QRectF(x + 1, (height() - pixels) / 2. + 1, pixels - 2, pixels - 2),
                        30 * 16, 270 * 16);
        painter.setPen(foreground);
    } else if (!icon().isNull()) {
        if (designIcon_ && (paintedIcon_.isNull() || paintedIconColor_ != foreground)) {
            paintedIcon_ = themedIcon(*designIcon_, foreground, pixels);
            paintedIconColor_ = foreground;
        }
        const auto renderedIcon = designIcon_ ? paintedIcon_ : icon();
        renderedIcon.paint(&painter, QRect(x, (height() - pixels) / 2, pixels, pixels),
                           Qt::AlignCenter, QIcon::Normal);
    }
    if (hasLeading)
        x += pixels + gap;
    if (!iconOnly)
        painter.drawText(QRect(x, 0, textWidth, height()), Qt::AlignCenter | Qt::TextShowMnemonic,
                         label);
}
Button* createDismissButton(const QString& accessibleName, QWidget* parent) {
    auto* dismiss = new Button({}, parent);
    dismiss->setAccessibleName(accessibleName);
    dismiss->setToolTip(accessibleName);
    dismiss->setVariant(ButtonVariant::Ghost);
    dismiss->setButtonSize(ButtonSize::IconSmall);
    dismiss->setDesignIcon(Icon::Close);
    dismiss->setAutoDefault(false);
    return dismiss;
}
}
