#include "design_system/button/button.h"
#include "design_system/theme.h"
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionButton>
#include <QtMath>

namespace choscordb::design {
namespace {
int buttonPadding(ButtonSize size, ButtonContext context) {
    const int index = static_cast<int>(size) % 4;
    return context == ButtonContext::EditorAction ? 12 : index == 0 ? 6 : index == 1 ? 9 : 12;
}
QFont buttonFont(ButtonSize size, ButtonContext context) {
    const int index = static_cast<int>(size) % 4;
    const auto role = context == ButtonContext::EditorAction ? TypographyRole::Field
                      : index == 0                           ? TypographyRole::SectionCaption
                      : index == 1                           ? TypographyRole::Small
                                                             : TypographyRole::Ui;
    auto font = resolveTypography(role);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    font.setWeight(QFont::Normal);
    return font;
}
} // namespace

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
    QPushButton::setIcon(themedIcon(role, resolvedThemeForWidget(*this).colors.foreground,
                                    dimension(Dimension::Icon)));
    setButtonSize(size_);
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
    setFont(buttonFont(size_, context_));
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
    setButtonSize(size_);
    updateGeometry();
    update();
}
bool Button::isLoading() const {
    return loading_;
}
QSize Button::sizeHint() const {
    const int index = static_cast<int>(size_) % 4;
    const int heights[] = {dimension(Dimension::ButtonExtraSmall),
                           dimension(Dimension::ButtonSmall), dimension(Dimension::Button),
                           dimension(Dimension::ButtonLarge)};
    const bool hasLeading = loading_ || !icon().isNull() || size_ >= ButtonSize::IconExtraSmall;
    const int height = context_ == ButtonContext::Choice
                           ? DesignMetrics{}.connectionDriverHeight
                           : heights[index] + (hasLeading ? (index == 0   ? 0
                                                             : index == 1 ? 1
                                                                          : 3)
                                                          : 0);
    if (size_ >= ButtonSize::IconExtraSmall)
        return {height, height};
    const int padding = buttonPadding(size_, context_);
    return {qCeil(QFontMetricsF(buttonFont(size_, context_)).horizontalAdvance(text())) +
                2 * padding + 2 +
                (!loading_ && icon().isNull()
                     ? 0
                     : (index == 0 ? dimension(Dimension::IconSmall) : dimension(Dimension::Icon)) +
                           spacing(Spacing::One)),
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
    // Window activation and popup dismissal restore focus without changing its origin.
    if (event->reason() == Qt::MouseFocusReason)
        keyboardFocus_ = false;
    else if (event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason ||
             event->reason() == Qt::ShortcutFocusReason)
        keyboardFocus_ = true;
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
        background = colors.primary;
        foreground = colors.primaryForeground;
        break;
    case ButtonVariant::Secondary:
        background = colors.secondary;
        foreground = colors.secondaryForeground;
        if (hover)
            background = colors.accent;
        break;
    case ButtonVariant::Outline:
        background = hover ? colors.muted : colors.surface;
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
    if (context_ == ButtonContext::SidebarTab && isChecked()) {
        background = colors.muted;
        foreground = colors.sidebarForeground;
    }
    if (context_ == ButtonContext::Choice && isChecked()) {
        background = colors.subtleAccent;
        foreground = colors.sidebarAccentForeground;
        border = colors.primary;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!state.testFlag(QStyle::State_Enabled))
        painter.setOpacity(.4);
    const bool focus = state.testFlag(QStyle::State_HasFocus);
    QRectF panel = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
    const int radius = design::radius(Radius::Large);
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
        painter.fillPath(path, background);
        painter.setBrush(Qt::NoBrush);
        // The next button paints the shared edge; painting both makes a two-pixel line.
        if (!last) {
            painter.save();
            painter.setClipRect(vertical ? QRect(0, 0, width(), height() - 1)
                                         : QRect(0, 0, width() - 1, height()));
            painter.drawPath(path);
            painter.restore();
        } else
            painter.drawPath(path);
    } else
        painter.drawRoundedRect(panel, radius, radius);
    if (focus) {
        painter.setPen(
            QPen(variant_ == ButtonVariant::Default ? colors.primaryForeground : colors.focus,
                 focusSpec().borderWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(panel.adjusted(1, 1, -1, -1), radius, radius);
    }
    auto textFont = buttonFont(size_, context_);
    const int index = static_cast<int>(size_) % 4;
    textFont.setUnderline(variant_ == ButtonVariant::Link && hover);
    painter.setFont(textFont);
    painter.setPen(foreground);
    const bool iconOnly = size_ >= ButtonSize::IconExtraSmall;
    const int iconPixels =
        index == 0 ? dimension(Dimension::IconSmall) : dimension(Dimension::Icon);
    const int gap = spacing(Spacing::One);
    const bool hasLeading = loading_ || !icon().isNull();
    const auto label =
        painter.fontMetrics().elidedText(text(), Qt::ElideRight,
                                         qMax(0, width() - 2 * buttonPadding(size_, context_) - 2 -
                                                     (hasLeading ? iconPixels + gap : 0)));
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
            paintedIcon_ = themedIcon(*designIcon_, foreground, iconPixels);
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
} // namespace choscordb::design
