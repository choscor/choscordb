#include "design_system/control_style.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDynamicPropertyChangeEvent>
#include <QFocusEvent>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSpinBox>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTextEdit>
#include <QTextLayout>
#include <QTimer>
#include <QtMath>

namespace choscordb::design {
namespace {
QVariant scopedThemeValue(const QWidget* widget) {
    for (auto* ancestor = widget; ancestor; ancestor = ancestor->parentWidget()) {
        const auto value = ancestor->property("designTheme");
        if (value.canConvert<ResolvedTheme>()) {
            return value;
        }
    }
    return qApp->property("designTheme");
}
// QStyleSheetStyle paints styled arrow rules itself, bypassing proxy primitives.
// This shared child paints only those glyphs; Qt keeps all hit testing and input.
class ControlGlyphOverlay final : public QWidget {
  public:
    explicit ControlGlyphOverlay(QWidget* owner) : QWidget(owner) {
        setObjectName("designControlGlyphs");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(owner->rect());
        if (auto* spin = qobject_cast<QSpinBox*>(owner)) {
            connect(spin, &QSpinBox::valueChanged, this, [this] { update(); });
        } else if (auto* spin = qobject_cast<QDoubleSpinBox*>(owner)) {
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { update(); });
        }
        show();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        auto* owner = parentWidget();
        if (!owner) {
            return;
        }
        auto color = owner->palette().color(QPalette::ButtonText);
        const auto themeValue = scopedThemeValue(owner);
        if (themeValue.canConvert<ResolvedTheme>()) {
            color = themeValue.value<ResolvedTheme>().colors.mutedText;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto drawArrow = [&](const QRect& area, bool up, bool enabled, qreal size) {
            if (area.isEmpty()) {
                return;
            }
            painter.save();
            painter.setOpacity(enabled ? 1.0 : 0.5);
            painter.translate(QRectF(area).center());
            if (up) {
                painter.rotate(180);
            }
            painter.scale(size / 16.0, size / 16.0);
            painter.setPen(QPen(color, 4.0 / 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath arrow;
            arrow.moveTo(-4, -2);
            arrow.lineTo(0, 2);
            arrow.lineTo(4, -2);
            painter.drawPath(arrow);
            painter.restore();
        };
        if (auto* combo = qobject_cast<QComboBox*>(owner)) {
            QStyleOptionComboBox option;
            option.initFrom(combo);
            option.editable = combo->isEditable();
            option.frame = combo->hasFrame();
            drawArrow(combo->style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                     QStyle::SC_ComboBoxArrow, combo),
                      false, combo->isEnabled(), 16);
        } else if (auto* spin = qobject_cast<QAbstractSpinBox*>(owner)) {
            if (spin->buttonSymbols() == QAbstractSpinBox::NoButtons) {
                return;
            }
            QStyleOptionSpinBox option;
            option.initFrom(spin);
            option.frame = spin->hasFrame();
            option.buttonSymbols = spin->buttonSymbols();
            bool up = spin->isEnabled() && !spin->isReadOnly();
            bool down = up;
            if (!spin->wrapping()) {
                if (auto* integer = qobject_cast<QSpinBox*>(spin)) {
                    up = up && integer->value() < integer->maximum();
                    down = down && integer->value() > integer->minimum();
                } else if (auto* real = qobject_cast<QDoubleSpinBox*>(spin)) {
                    up = up && real->value() < real->maximum();
                    down = down && real->value() > real->minimum();
                }
            }
            drawArrow(spin->style()->subControlRect(QStyle::CC_SpinBox, &option,
                                                    QStyle::SC_SpinBoxUp, spin),
                      true, up, 12);
            drawArrow(spin->style()->subControlRect(QStyle::CC_SpinBox, &option,
                                                    QStyle::SC_SpinBoxDown, spin),
                      false, down, 12);
        }
    }
};
class MenuShadowEffect final : public QGraphicsEffect {
  public:
    explicit MenuShadowEffect(QObject* parent) : QGraphicsEffect(parent) {}

  protected:
    void draw(QPainter* painter) override {
        QPoint offset;
        const auto source = sourcePixmap(Qt::LogicalCoordinates, &offset, NoPad);
        if (source.isNull()) {
            return;
        }
        if (source.cacheKey() != sourceKey_) {
            sourceKey_ = source.cacheKey();
            QPixmap silhouette(source.size());
            silhouette.setDevicePixelRatio(source.devicePixelRatio());
            silhouette.fill(Qt::transparent);
            {
                QPainter mask(&silhouette);
                mask.drawPixmap(0, 0, source);
                mask.setCompositionMode(QPainter::CompositionMode_SourceIn);
                mask.fillRect(silhouette.rect(), Qt::black);
            }
            shadow_ = QPixmap(source.size());
            shadow_.setDevicePixelRatio(source.devicePixelRatio());
            shadow_.fill(Qt::transparent);
            QPainter combined(&shadow_);
            const auto size = source.deviceIndependentSize();
            // CSS shadow-md: (0,4,6,-1,.1) + (0,2,4,-2,.1).
            // Qt's blur diameter is twice the CSS blur radius.
            for (const auto& layer : elevation(Elevation::Medium)) {
                QGraphicsScene scene;
                auto* item = scene.addPixmap(silhouette);
                const qreal spread = -layer.spread;
                item->setTransform(
                    QTransform::fromScale((size.width() - 2 * spread) / size.width(),
                                          (size.height() - 2 * spread) / size.height()));
                item->setPos(spread, spread);
                auto* effect = new QGraphicsDropShadowEffect;
                effect->setBlurRadius(layer.blur * 2);
                effect->setOffset(layer.x, layer.y);
                auto shade = QColor(Qt::black);
                shade.setAlphaF(layer.opacity);
                effect->setColor(shade);
                item->setGraphicsEffect(effect);
                scene.render(&combined, QRectF(QPointF(), size), QRectF(QPointF(), size));
            }
        }
        painter->drawPixmap(offset, shadow_);
        painter->drawPixmap(offset, source);
    }

  private:
    qint64 sourceKey_ = 0;
    QPixmap shadow_;
};
class TooltipSurface final : public QWidget {
  public:
    TooltipSurface(const QString& text, QWidget* owner)
        : QWidget(owner->window(), Qt::ToolTip), owner_(owner) {
        setObjectName("designTooltip");
        setAccessibleName(text);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFont(resolveTypography(TypographyRole::Small));
        const auto available = owner->screen()->availableGeometry();
        const qreal contentWidth = qMax(1, qMin(296, available.width() - 40));
        const qreal contentHeight = qMax(1, available.height() - 34);
        textLayout_.setText(text);
        textLayout_.setFont(font());
        QTextOption textOption;
        textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        textLayout_.setTextOption(textOption);
        textLayout_.beginLayout();
        qreal layoutHeight = 0;
        qreal layoutWidth = 0;
        while (true) {
            auto line = textLayout_.createLine();
            if (!line.isValid()) {
                break;
            }
            line.setLineWidth(contentWidth);
            if (layoutHeight + line.height() > contentHeight) {
                line.setPosition(QPointF(0, contentHeight));
                break;
            }
            line.setPosition(QPointF(0, layoutHeight));
            layoutHeight += line.height();
            layoutWidth = qMax(layoutWidth, line.naturalTextWidth());
        }
        textLayout_.endLayout();
        resize(qCeil(layoutWidth) + 24, qCeil(layoutHeight) + 18);
        const auto anchor = owner->mapToGlobal(QPoint(owner->width() / 2, 0));
        int y = anchor.y() - height() - 4;
        below_ = y < available.top();
        if (below_) {
            y = owner->mapToGlobal(QPoint(0, owner->height())).y() + 4;
        }
        const int x =
            qBound(available.left(), anchor.x() - width() / 2, available.right() - width() + 1);
        move(x, qBound(available.top(), y, available.bottom() - height() + 1));
        arrowX_ = qBound(12, anchor.x() - x, width() - 12);
        connect(owner, &QObject::destroyed, this, &QWidget::hide);
        QTimer::singleShot(10000, this, &QWidget::hide);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QVariant themeValue;
        for (auto* ancestor = owner_.data(); ancestor; ancestor = ancestor->parentWidget()) {
            if (ancestor->property("designTheme").isValid()) {
                themeValue = ancestor->property("designTheme");
                break;
            }
        }
        if (!themeValue.isValid()) {
            themeValue = qApp->property("designTheme");
        }
        const auto colors = themeValue.canConvert<ResolvedTheme>()
                                ? themeValue.value<ResolvedTheme>().colors
                                : resolveColors(palette().color(QPalette::Window).lightness() < 128
                                                    ? ResolvedAppearance::Dark
                                                    : ResolvedAppearance::Light,
                                                {});
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.foreground);
        const QRectF body(0, below_ ? 6 : 0, width(), height() - 6);
        painter.drawRoundedRect(body, 8, 8);
        QPainterPath arrow;
        const qreal base = below_ ? 6 : height() - 6;
        arrow.moveTo(arrowX_ - 5, base);
        arrow.lineTo(arrowX_, below_ ? 1 : height() - 1);
        arrow.lineTo(arrowX_ + 5, base);
        arrow.closeSubpath();
        painter.drawPath(arrow);
        painter.setPen(colors.background);
        painter.setClipRect(body.adjusted(12, 6, -12, -6));
        textLayout_.draw(&painter, QPointF(12, body.top() + 6));
    }

  private:
    QTextLayout textLayout_;
    QPointer<QWidget> owner_;
    bool below_ = false;
    int arrowX_ = 0;
};
class FieldFocusFrame final : public QFocusFrame {
  public:
    explicit FieldFocusFrame(QWidget* parent) : QFocusFrame(parent) {}

  protected:
    void paintEvent(QPaintEvent*) override {
        if (widget() == nullptr) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto palette = widget()->window()->palette();
        auto color = widget()->property("invalid").toBool() ? palette.color(QPalette::BrightText)
                                                            : palette.color(QPalette::Dark);
        QVariant themeValue;
        for (auto* ancestor = widget(); ancestor; ancestor = ancestor->parentWidget()) {
            if (ancestor->property("designTheme").isValid()) {
                themeValue = ancestor->property("designTheme");
                break;
            }
        }
        if (!themeValue.isValid()) {
            themeValue = qApp->property("designTheme");
        }
        if (themeValue.canConvert<ResolvedTheme>()) {
            const auto theme = themeValue.value<ResolvedTheme>();
            color = widget()->property("invalid").toBool() ? theme.colors.destructive
                                                           : theme.colors.focus;
        }
        painter.setPen(QPen(color, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 12, 12);
    }
};
bool isField(const QWidget* widget) {
    return qobject_cast<const QLineEdit*>(widget) || qobject_cast<const QComboBox*>(widget) ||
           qobject_cast<const QAbstractSpinBox*>(widget) ||
           qobject_cast<const QPlainTextEdit*>(widget) || qobject_cast<const QTextEdit*>(widget) ||
           qobject_cast<const QKeySequenceEdit*>(widget);
}
} // namespace
void ControlStyle::polish(QWidget* widget) {
    QProxyStyle::polish(widget);
    if (qobject_cast<QMenu*>(widget) && !widget->graphicsEffect()) {
        widget->setAttribute(Qt::WA_TranslucentBackground);
        widget->setGraphicsEffect(new MenuShadowEffect(widget));
    }
    if ((qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractSpinBox*>(widget)) &&
        !widget->findChild<QWidget*>("designControlGlyphs", Qt::FindDirectChildrenOnly)) {
        new ControlGlyphOverlay(widget);
    }
    widget->installEventFilter(this);
}
void ControlStyle::unpolish(QWidget* widget) {
    widget->removeEventFilter(this);
    delete widget->findChild<QWidget*>("designControlGlyphs", Qt::FindDirectChildrenOnly);
    if (focusFrame_ && focusFrame_->widget() == widget) {
        focusFrame_->setWidget(nullptr);
    }
    QProxyStyle::unpolish(widget);
}
bool ControlStyle::eventFilter(QObject* watched, QEvent* event) {
    auto* field = qobject_cast<QWidget*>(watched);
    if (field &&
        (event->type() == QEvent::Resize || event->type() == QEvent::StyleChange ||
         event->type() == QEvent::PaletteChange || event->type() == QEvent::EnabledChange)) {
        if (auto* glyphs =
                field->findChild<QWidget*>("designControlGlyphs", Qt::FindDirectChildrenOnly)) {
            glyphs->setGeometry(field->rect());
            glyphs->raise();
            glyphs->update();
        }
    }
    if (field && event->type() == QEvent::DynamicPropertyChange) {
        const auto name = static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName();
        if (name == "invalid" || name == "state" || name == "variant" || name == "designRole" ||
            name == "primary" || name == "iconOnly") {
            const bool restoreFocus = focusFrame_ && focusFrame_->widget() == field;
            field->style()->unpolish(field);
            field->style()->polish(field);
            if (restoreFocus && focusFrame_) {
                focusFrame_->setWidget(field);
                focusFrame_->clearMask();
                focusFrame_->raise();
            }
            field->update();
        }
    }
    if (field && event->type() == QEvent::ToolTip && !field->toolTip().isEmpty()) {
        if (tooltip_) {
            delete tooltip_.data();
        }
        tooltip_ = new TooltipSurface(field->toolTip(), field);
        tooltipOwner_ = field;
        tooltip_->show();
        event->accept();
        return true;
    }
    if (tooltip_ &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress ||
         (field == tooltipOwner_ &&
          (event->type() == QEvent::Leave || event->type() == QEvent::Hide)))) {
        tooltip_->hide();
    }
    if (field && isField(field) && event->type() == QEvent::FocusIn) {
        const auto reason = static_cast<QFocusEvent*>(event)->reason();
        if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
            reason == Qt::ShortcutFocusReason) {
            auto* target = isField(field->parentWidget()) ? field->parentWidget() : field;
            if (!focusFrame_) {
                focusFrame_ = new FieldFocusFrame(target);
            }
            focusFrame_->setWidget(target);
            focusFrame_->clearMask();
            focusFrame_->raise();
        }
    } else if (field && event->type() == QEvent::FocusOut && focusFrame_) {
        focusFrame_->setWidget(nullptr);
    }
    return QProxyStyle::eventFilter(watched, event);
}
ControlStyle::ControlStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
void ControlStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                                 QPainter* painter, const QWidget* widget) const {
    if (element == PE_IndicatorTabClose) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const auto color = widget ? resolvedThemeForWidget(*widget).colors.foreground
                                  : option->palette.color(QPalette::ButtonText);
        painter->setOpacity(option->state.testFlag(State_Enabled) ? 0.7 : 0.35);
        painter->translate(QRectF(option->rect).center());
        painter->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(-3, -3), QPointF(3, 3));
        painter->drawLine(QPointF(-3, 3), QPointF(3, -3));
        painter->restore();
        return;
    }
    if (element == PE_IndicatorBranch) {
        if (option->state.testFlag(State_Children)) {
            const auto arrow =
                option->state.testFlag(State_Open)
                    ? PE_IndicatorArrowDown
                    : (option->direction == Qt::RightToLeft ? PE_IndicatorArrowLeft
                                                            : PE_IndicatorArrowRight);
            drawPrimitive(arrow, option, painter, widget);
        }
        return;
    }
    if (element == PE_FrameFocusRect) {
        if (option->state.testFlag(State_KeyboardFocusChange)) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(option->palette.color(QPalette::Dark), 3));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(QRectF(option->rect).adjusted(1.5, 1.5, -1.5, -1.5), 6, 6);
            painter->restore();
        }
        return;
    }
    if (element == PE_IndicatorArrowDown || element == PE_IndicatorArrowUp ||
        element == PE_IndicatorArrowLeft || element == PE_IndicatorArrowRight ||
        element == PE_IndicatorSpinDown || element == PE_IndicatorSpinUp) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(QRectF(option->rect).center());
        if (element == PE_IndicatorArrowUp || element == PE_IndicatorSpinUp) {
            painter->rotate(180);
        } else if (element == PE_IndicatorArrowLeft) {
            painter->rotate(90);
        } else if (element == PE_IndicatorArrowRight) {
            painter->rotate(-90);
        }
        if (!option->state.testFlag(State_Enabled)) {
            painter->setOpacity(0.5);
        }
        painter->setPen(QPen(option->palette.color(QPalette::ButtonText), 1.5, Qt::SolidLine,
                             Qt::RoundCap, Qt::RoundJoin));
        QPainterPath chevron;
        chevron.moveTo(-4, -2);
        chevron.lineTo(0, 2);
        chevron.lineTo(4, -2);
        painter->drawPath(chevron);
        painter->restore();
        return;
    }
    if (element == PE_IndicatorCheckBox || element == PE_IndicatorItemViewItemCheck) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const bool enabled = option->state.testFlag(State_Enabled);
        const bool checked = option->state.testFlag(State_On);
        const bool mixed = option->state.testFlag(State_NoChange);
        if (!enabled) {
            painter->setOpacity(0.5);
        }
        const auto rectangle = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        auto fill = option->palette.color(QPalette::Active, QPalette::Accent);
        auto markColor = option->palette.color(QPalette::Active, QPalette::HighlightedText);
        auto border = option->palette.color(QPalette::Mid);
        const auto themeValue = scopedThemeValue(widget);
        if (themeValue.canConvert<ResolvedTheme>()) {
            const auto colors = themeValue.value<ResolvedTheme>().colors;
            fill = colors.primary;
            markColor = colors.primaryForeground;
            border = colors.input;
        }
        painter->setPen(QPen(checked || mixed ? fill : border, 1));
        painter->setBrush(checked || mixed ? QBrush(fill) : Qt::NoBrush);
        painter->drawRoundedRect(rectangle, 4, 4);
        if (checked || mixed) {
            painter->setPen(QPen(markColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const auto origin = QPointF(option->rect.topLeft());
            if (mixed) {
                painter->drawLine(origin + QPointF(4, 8), origin + QPointF(12, 8));
            } else {
                QPainterPath mark;
                mark.moveTo(origin + QPointF(3.5, 8));
                mark.lineTo(origin + QPointF(6.5, 11));
                mark.lineTo(origin + QPointF(12.5, 5));
                painter->drawPath(mark);
            }
        }
        painter->restore();
        return;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}
int ControlStyle::pixelMetric(PixelMetric metric, const QStyleOption* option,
                              const QWidget* widget) const {
    if (metric == PM_HeaderDefaultSectionSizeVertical)
        return dimension(Dimension::TableRow);
    if (metric == PM_FocusFrameHMargin || metric == PM_FocusFrameVMargin) {
        return 3;
    }
    if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight) {
        return 16;
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}
QString controlStyleSheet(const ResolvedTheme& theme) {
    const auto cssColor = [](const QColor& color) {
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(color.alpha());
    };
    QString sheet = QStringLiteral(R"(
QPushButton { min-height: 30px; padding: 0 10px; color: @foreground; }
QPushButton[variant="default"] { background: @primary; color: @primaryText; border-color: transparent; font-weight: 500; }
QPushButton[variant="outline"] { background: @field; color: @foreground; border-color: @border; font-weight: 500; }
QPushButton[variant="outline"]:hover, QPushButton[variant="outline"]:pressed { background: @muted; color: @foreground; }
QPushButton[variant="destructive"] { background: @destructiveTint; color: @dangerText; border-color: transparent; font-weight: 500; }
QPushButton[variant="destructive"]:hover, QPushButton[variant="destructive"]:pressed { background: @destructiveHover; color: @dangerText; }
QPushButton[variant="default"]:focus, QPushButton[variant="outline"]:focus, QPushButton[variant="destructive"]:focus { border-color: @focus; }
QPushButton[variant="default"]:disabled, QPushButton[variant="outline"]:disabled, QPushButton[variant="destructive"]:disabled { color: @disabled; }
QLineEdit, QPlainTextEdit, QTextEdit { selection-background-color: @primary; selection-color: @primaryText; }
QLabel, QCheckBox, QRadioButton, QGroupBox { color: @foreground; }
QLabel[designRole="heading"] { font-size: 16px; font-weight: 500; color: @foreground; }
QLabel[designRole="description"] { color: @mutedText; }
QLabel[designRole="kbd"] { background: @muted; color: @mutedText; border-radius: 6px; padding: 2px 4px; font-size: 12px; }
QLabel[designRole="badge"] { min-height: 18px; max-height: 18px; border: 1px solid transparent; border-radius: 10px; padding: 0 8px; font-size: 12px; font-weight: 500; color: @primaryText; background: @primary; }
QLabel[designRole="badge"][variant="secondary"] { background: @muted; color: @foreground; }
QLabel[designRole="badge"][variant="outline"] { background: transparent; color: @foreground; border-color: @border; }
QLabel[designRole="badge"][variant="destructive"] { background: @destructiveTint; color: @dangerText; }
QProgressBar { min-height: 4px; max-height: 4px; border: 0; border-radius: 2px; background: @muted; }
QProgressBar::chunk { border-radius: 2px; background: @primary; }
QToolTip { background: @foreground; color: @background; border: 0; border-radius: 8px; padding: 6px 12px; font-size: 12px; }
QSplitter::handle { background: @border; }
QSplitter::handle:hover, QSplitter::handle:pressed { background: @focus; }
QFrame[frameShape="4"] { max-height: 1px; border: 0; background: @border; }
QFrame[frameShape="5"] { max-width: 1px; border: 0; background: @border; }
QTabWidget::pane { border: 0; }
QTabBar { background: @muted; border-radius: 10px; }
QTabBar::tab { min-height: 24px; max-height: 24px; border: 1px solid transparent; border-radius: 8px; padding: 0 6px; margin: 3px 0; color: @mutedText; background: transparent; }
QTabBar::tab:selected { background: @background; color: @foreground; }
QTabBar::tab:hover { color: @foreground; }
QTabBar::tab:disabled { color: @disabled; }
QToolButton { icon-size: 16px; min-height: 30px; max-height: 30px; border: 1px solid transparent; border-radius: 10px; padding: 0 10px; background: transparent; color: @foreground; }
QToolButton[iconOnly="true"] { min-width: 30px; max-width: 30px; padding: 0; }
QToolButton:hover, QToolButton:checked { background: @muted; color: @foreground; }
QToolButton:pressed { background: @accent; }
QToolButton:focus { border-color: @focus; }
QToolButton:disabled { color: @disabled; }
QToolButton::menu-indicator { width: 12px; height: 12px; subcontrol-position: right center; }
QToolBar { spacing: 4px; border: 0; background: @background; }
QToolBar::separator { background: @border; width: 1px; margin: 4px; }
QTableView, QTreeView, QListView { background: @background; color: @foreground; border: 0; gridline-color: @border; selection-background-color: @muted; selection-color: @foreground; outline: 0; }
QTableView::item { padding: 8px; }
QTreeView::item, QListView::item { min-height: 20px; padding: 4px 6px; border-radius: 8px; }
QTableView::item:selected, QTreeView::item:selected, QListView::item:selected { background: @muted; color: @foreground; }
QTableView::item:hover, QTreeView::item:hover, QListView::item:hover { background: @accent; }
QHeaderView { background: @background; color: @foreground; }
QHeaderView::section { min-height: 39px; border: 0; border-bottom: 1px solid @border; padding: 0 8px; background: @background; color: @foreground; font-weight: 500; }
QTableCornerButton::section { border: 0; border-bottom: 1px solid @border; background: @background; }
QMenu { margin: @shadowMargin; background: @popover; color: @popoverText; border: 1px solid @border; border-radius: 10px; padding: 4px; }
QMenu::item { min-height: 20px; padding: 4px 24px 4px 6px; border-radius: 8px; }
QMenu::item:selected { background: @accent; color: @accentText; }
QMenu::item:disabled { color: @disabled; }
QMenu::separator { height: 1px; background: @border; margin: 4px 0; }
QMenu::indicator { width: 16px; height: 16px; }
QScrollBar:vertical { width: 10px; margin: 0; border: 0; background: transparent; }
QScrollBar:horizontal { height: 10px; margin: 0; border: 0; background: transparent; }
QScrollBar::handle { background: @border; border: 2px solid transparent; border-radius: 5px; min-width: 20px; min-height: 20px; }
QScrollBar::handle:hover, QScrollBar::handle:pressed { background: @mutedText; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: 0; background: transparent; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QKeySequenceEdit {
  min-height: 30px; padding: 0 10px; border: 1px solid @input;
  border-radius: 10px; color: @foreground; background: @field;
}
QPlainTextEdit, QTextEdit { border: 1px solid @input; border-radius: 10px; padding: 8px; background: @field; color: @foreground; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus,
QPlainTextEdit:focus, QTextEdit:focus, QKeySequenceEdit:focus { border-color: @focus; }
QLineEdit[invalid="true"], QComboBox[invalid="true"], QSpinBox[invalid="true"],
QDoubleSpinBox[invalid="true"], QPlainTextEdit[invalid="true"], QTextEdit[invalid="true"],
QKeySequenceEdit[invalid="true"] { border-color: @destructive; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled,
QPlainTextEdit:disabled, QTextEdit:disabled { color: @disabled; }
QComboBox { padding-right: 28px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: right; width: 24px; border: 0; }
QComboBox::down-arrow { width: 16px; height: 16px; }
QComboBox QAbstractItemView { background: @popover; color: @popoverText; border: 1px solid @border; padding: 4px; outline: 0; selection-background-color: @accent; selection-color: @accentText; }
QComboBox QAbstractItemView::item { min-height: 20px; padding: 4px 6px; border-radius: 8px; }
QSpinBox, QDoubleSpinBox { padding-right: 24px; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: padding; subcontrol-position: top right; width: 20px; height: 14px; border: 0; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: padding; subcontrol-position: bottom right; width: 20px; height: 14px; border: 0; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow, QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { width: 12px; height: 12px; }
)");
    auto field = theme.colors.background;
    if (theme.appearance == ResolvedAppearance::Dark && !theme.forcedContrast) {
        field = QColor(255, 255, 255, 11);
    }
    auto destructiveTint = theme.colors.destructive;
    destructiveTint.setAlphaF(theme.appearance == ResolvedAppearance::Dark ? 0.2 : 0.1);
    auto destructiveHover = theme.colors.destructive;
    destructiveHover.setAlphaF(theme.appearance == ResolvedAppearance::Dark ? 0.3 : 0.2);
    int shadowMargin = 0;
    for (const auto& layer : elevation(Elevation::Medium)) {
        shadowMargin = qMax(shadowMargin, layer.blur * 2 + qMax(qAbs(layer.x), qAbs(layer.y)) +
                                              qMax(0, layer.spread));
    }
    sheet.replace("@shadowMargin", QString::number(shadowMargin) + "px");
    sheet.replace("@destructiveTint", cssColor(destructiveTint));
    sheet.replace("@destructiveHover", cssColor(destructiveHover));
    sheet.replace("@primaryText", cssColor(theme.colors.primaryForeground));
    sheet.replace("@primary", cssColor(theme.colors.primary));
    sheet.replace("@background", cssColor(theme.colors.background));
    sheet.replace("@mutedText", cssColor(theme.colors.mutedText));
    sheet.replace("@muted", cssColor(theme.colors.muted));
    sheet.replace("@input", cssColor(theme.colors.input));
    sheet.replace("@foreground", cssColor(theme.colors.foreground));
    sheet.replace("@field", cssColor(field));
    sheet.replace("@focus", cssColor(theme.colors.focus));
    sheet.replace("@dangerText", cssColor(theme.colors.danger));
    sheet.replace("@destructive", cssColor(theme.colors.destructive));
    sheet.replace("@disabled", cssColor(theme.colors.disabled));
    sheet.replace("@popoverText", cssColor(theme.colors.popoverForeground));
    sheet.replace("@popover", cssColor(theme.colors.popover));
    sheet.replace("@border", cssColor(theme.colors.border));
    sheet.replace("@accentText", cssColor(theme.colors.accentForeground));
    sheet.replace("@accent", cssColor(theme.colors.accent));
    return sheet;
}
} // namespace choscordb::design
