#include "design_system/tooltip/tooltip.h"
#include "design_system/theme.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QHeaderView>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QTabBar>
#include <QTextLayout>
#include <QTimer>
#include <QWidget>
#include <QtMath>

namespace choscordb::design::detail {
namespace {
int tooltipShadowMargin() {
    int margin = 0;
    for (const auto& layer : elevation(Elevation::Tooltip))
        margin = qMax(margin, layer.blur + qMax(qAbs(layer.x), qAbs(layer.y)));
    return margin;
}
struct KumoArrow final {
    QPainterPath fill;
    QPainterPath edge;
    QPainterPath stroke;
};
KumoArrow buildKumoArrow() {
    QPainterPath fill;
    QPainterPath edge;
    QPainterPath stroke;
    fill.moveTo(9.66437, 2.60207);
    fill.lineTo(4.80758, 6.97318);
    fill.cubicTo(4.07308, 7.63423, 3.11989, 8, 2.13172, 8);
    fill.lineTo(0, 8);
    fill.lineTo(0, 10);
    fill.lineTo(20, 10);
    fill.lineTo(20, 8);
    fill.lineTo(18.5349, 8);
    fill.cubicTo(17.5468, 8, 16.5936, 7.63423, 15.8591, 6.97318);
    fill.lineTo(11.0023, 2.60207);
    fill.cubicTo(10.622, 2.2598, 10.0447, 2.25979, 9.66437, 2.60207);
    fill.closeSubpath();
    edge.moveTo(8.99542, 1.85876);
    edge.cubicTo(9.75604, 1.17425, 10.9106, 1.17422, 11.6713, 1.85878);
    edge.lineTo(16.5281, 6.22989);
    edge.cubicTo(17.0789, 6.72568, 17.7938, 7.00001, 18.5349, 7.00001);
    edge.lineTo(15.89, 7);
    edge.lineTo(11.0023, 2.60207);
    edge.cubicTo(10.622, 2.2598, 10.0447, 2.2598, 9.66436, 2.60207);
    edge.lineTo(4.77734, 7);
    edge.lineTo(2.13171, 7.00001);
    edge.cubicTo(2.87284, 7.00001, 3.58774, 6.72568, 4.13861, 6.22989);
    edge.lineTo(8.99542, 1.85876);
    edge.closeSubpath();
    stroke.moveTo(10.3333, 3.34539);
    stroke.lineTo(5.47654, 7.71648);
    stroke.cubicTo(4.55842, 8.54279, 3.36693, 9, 2.13172, 9);
    stroke.lineTo(0, 9);
    stroke.lineTo(0, 8);
    stroke.lineTo(2.13172, 8);
    stroke.cubicTo(3.11989, 8, 4.07308, 7.63423, 4.80758, 6.97318);
    stroke.lineTo(9.66437, 2.60207);
    stroke.cubicTo(10.0447, 2.25979, 10.622, 2.2598, 11.0023, 2.60207);
    stroke.lineTo(15.8591, 6.97318);
    stroke.cubicTo(16.5936, 7.63423, 17.5468, 8, 18.5349, 8);
    stroke.lineTo(20, 8);
    stroke.lineTo(20, 9);
    stroke.lineTo(18.5349, 9);
    stroke.cubicTo(17.2998, 9, 16.1083, 8.54278, 15.1901, 7.71648);
    stroke.lineTo(10.3333, 3.34539);
    stroke.closeSubpath();
    return {fill, edge, stroke};
}
const KumoArrow& kumoArrow() {
    static const KumoArrow arrow = buildKumoArrow();
    return arrow;
}
}
class TooltipSurface final : public QWidget {
  public:
    TooltipSurface(const QString& text, QWidget* owner, const QRect& anchorRect)
        : QWidget(owner->window(), Qt::Widget), owner_(owner) {
        setObjectName("designTooltip");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setProperty("embeddedPopupOwner", QVariant::fromValue<QObject*>(owner));
        setAccessibleName(text);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFont(resolveTypography(TextStyle::Callout));
        const auto available = owner->window()->rect();
        const int horizontal = spacing(Spacing::TwoHalf);
        const int vertical = spacing(Spacing::OneHalf);
        const qreal contentWidth =
            qMax(1, qMin(dimension(Dimension::ToastWidth) - 2 * horizontal,
                         available.width() - 2 * horizontal - 2 * spacing(Spacing::Two)));
        const qreal contentHeight = qMax(1, available.height() - 2 * vertical);
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
        const int shadow = tooltipShadowMargin();
        resize(QSize(qCeil(layoutWidth) + 2 * horizontal + 2 * shadow,
                     qCeil(layoutHeight) + 2 * vertical + 2 * shadow)
                   .boundedTo(available.size()));
        const auto anchor =
            owner->mapTo(owner->window(), QPoint(anchorRect.center().x(), anchorRect.top()));
        const int gap = spacing(Spacing::TwoHalf);
        int y = anchor.y() - height() + shadow - gap;
        if (y < available.top()) {
            y = owner->mapTo(owner->window(), QPoint(0, anchorRect.bottom() + 1)).y() + gap -
                shadow;
            arrowBelow_ = false;
        }
        const int x =
            qBound(available.left(), anchor.x() - width() / 2, available.right() - width() + 1);
        const int arrowReach =
            shadow + radius(Radius::Medium) + qCeil(kumoArrow().fill.boundingRect().width() / 2);
        arrowX_ = qBound(arrowReach, anchor.x() - x, qMax(arrowReach, width() - arrowReach));
        move(x, qBound(available.top(), y, available.bottom() - height() + 1));
        connect(owner, &QObject::destroyed, this, [this] {
            setProperty("embeddedPopupOwner", QVariant());
            hide();
        });
        owner->window()->installEventFilter(this);
        QTimer::singleShot(10000, this, &QWidget::hide);
    }

  protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Hide ||
            event->type() == QEvent::WindowDeactivate)
            hide();
        return false;
    }
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
                                                    : ResolvedAppearance::Light);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        const int shadow = tooltipShadowMargin();
        const QRectF body = QRectF(rect()).adjusted(shadow, shadow, -shadow, -shadow);
        const qreal rounding = radius(Radius::Medium);
        for (const auto& layer : elevation(Elevation::Tooltip)) {
            const auto cast =
                body.translated(layer.x, layer.y)
                    .adjusted(-layer.spread, -layer.spread, layer.spread, layer.spread);
            for (int step = 0; step < layer.blur; ++step) {
                const qreal grow = step - layer.blur / 2.0;
                auto tint = layer.color;
                tint.setAlphaF(layer.opacity / layer.blur);
                painter.setBrush(tint);
                painter.drawRoundedRect(cast.adjusted(-grow, -grow, grow, grow),
                                        qMax(0.0, rounding + grow), qMax(0.0, rounding + grow));
            }
        }
        painter.setBrush(colors.base);
        painter.setPen(QPen(colors.line, 1));
        painter.drawRoundedRect(body.adjusted(0.5, 0.5, -0.5, -0.5), rounding, rounding);
        paintArrow(painter, body, colors);
        const int horizontal = spacing(Spacing::TwoHalf);
        const int vertical = spacing(Spacing::OneHalf);
        painter.setPen(colors.textDefault);
        painter.setClipRect(body.adjusted(horizontal, vertical, -horizontal, -vertical));
        textLayout_.draw(&painter, body.topLeft() + QPointF(horizontal, vertical));
    }

  private:
    void paintArrow(QPainter& painter, const QRectF& body, const Colors& colors) const {
        const auto& arrow = kumoArrow();
        const QRectF viewBox(QPointF(), arrow.fill.boundingRect().bottomRight());
        const qreal reach = spacing(Spacing::Two) - viewBox.height() / 2;
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.translate(arrowX_, arrowBelow_ ? body.bottom() + reach : body.top() - reach);
        if (arrowBelow_)
            painter.rotate(180);
        painter.translate(-viewBox.center());
        painter.fillPath(arrow.fill, colors.base);
        painter.fillPath(arrow.edge, colors.arrowEdge);
        painter.fillPath(arrow.stroke, colors.arrowStroke);
        painter.restore();
    }

    QTextLayout textLayout_;
    QPointer<QWidget> owner_;
    int arrowX_ = 0;
    bool arrowBelow_ = true;
};
namespace {
QPointer<QWidget> tooltip_;
QPointer<QWidget> tooltipOwner_;
}
void showTooltip(QWidget* owner, const QRect& anchor, const QString& text) {
    if (tooltip_)
        delete tooltip_.data();
    tooltip_ = new TooltipSurface(text, owner, anchor);
    tooltipOwner_ = owner;
    tooltip_->show();
}
bool handleTooltipEvent(QWidget* field, QEvent* event) {
    if (field && event->type() == QEvent::ToolTip) {
        QString text = field->toolTip();
        QRect anchorRect = field->rect();
        const auto point = static_cast<QHelpEvent*>(event)->pos();
        if (auto* view = qobject_cast<QAbstractItemView*>(field->parentWidget());
            view && field == view->viewport()) {
            if (auto* header = qobject_cast<QHeaderView*>(view)) {
                const int section = header->logicalIndexAt(point);
                if (section >= 0 && header->model()) {
                    text = header->model()
                               ->headerData(section, header->orientation(), Qt::ToolTipRole)
                               .toString();
                    const int start = header->sectionViewportPosition(section);
                    const int size = header->sectionSize(section);
                    anchorRect = header->orientation() == Qt::Horizontal
                                     ? QRect(start, 0, size, field->height())
                                     : QRect(0, start, field->width(), size);
                }
            } else {
                const auto index = view->indexAt(point);
                text = index.data(Qt::ToolTipRole).toString();
                anchorRect = view->visualRect(index);
            }
        } else if (auto* tabs = qobject_cast<QTabBar*>(field)) {
            const int tab = tabs->tabAt(point);
            if (tab >= 0) {
                text = tabs->tabToolTip(tab);
                anchorRect = tabs->tabRect(tab);
            }
        }
        if (text.isEmpty()) {
            if (tooltip_) {
                tooltip_->hide();
            }
            return false;
        }
        showTooltip(field, anchorRect, text);
        event->accept();
        return true;
    }
    if (tooltip_ &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress ||
         (field == tooltipOwner_ &&
          (event->type() == QEvent::Leave || event->type() == QEvent::Hide)))) {
        tooltip_->hide();
    }
    return false;
}

}
