#include "design_system/tooltip/tooltip.h"
#include "design_system/theme.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QHeaderView>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTabBar>
#include <QTextLayout>
#include <QTimer>
#include <QWidget>
#include <QtMath>

namespace choscordb::design::detail {
class TooltipSurface final : public QWidget {
  public:
    TooltipSurface(const QString& text, QWidget* owner, const QRect& anchorRect)
        : QWidget(owner->window(), Qt::ToolTip | Qt::FramelessWindowHint), owner_(owner) {
        setObjectName("designTooltip");
        setAccessibleName(text);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
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
        const auto anchor = owner->mapToGlobal(QPoint(anchorRect.center().x(), anchorRect.top()));
        int y = anchor.y() - height() - 4;
        below_ = y < available.top();
        if (below_) {
            y = owner->mapToGlobal(QPoint(0, anchorRect.bottom() + 1)).y() + 4;
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
bool handleTooltipEvent(QWidget* field, QEvent* event, QPointer<QWidget>& tooltip_,
                        QPointer<QWidget>& tooltipOwner_) {
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
        if (tooltip_) {
            delete tooltip_.data();
        }
        tooltip_ = new TooltipSurface(text, field, anchorRect);
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
    return false;
}

} // namespace choscordb::design::detail
