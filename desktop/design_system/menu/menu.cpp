#include "design_system/menu/menu.h"
#include "design_system/theme.h"
#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QPainter>
#include <QScreen>

namespace choscordb::design::detail {
int menuShadowMargin() {
    int margin = 0;
    for (const auto& layer : elevation(Elevation::Medium))
        margin = qMax(margin,
                      layer.blur * 2 + qMax(qAbs(layer.x), qAbs(layer.y)) + qMax(0, layer.spread));
    return margin;
}
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
            // MVP app-menu-popup shadow; retain its tinted shadow color.
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
                auto shade = layer.color;
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
void polishMenu(QWidget* widget) {
    if (qobject_cast<QMenu*>(widget) && !widget->graphicsEffect()) {
        widget->setWindowFlag(Qt::NoDropShadowWindowHint);
        widget->setAttribute(Qt::WA_TranslucentBackground);
        widget->setGraphicsEffect(new MenuShadowEffect(widget));
    }
}
void positionSubmenu(QWidget* field, QEvent* event) {
    if (auto* menu = qobject_cast<QMenu*>(field); menu && event->type() == QEvent::Show) {
        auto* parentMenu = qobject_cast<QMenu*>(menu->parentWidget());
        if (parentMenu && parentMenu->isVisible() &&
            parentMenu->activeAction() == menu->menuAction()) {
            // Qt's screen-edge flip uses the whole window width, including our
            // transparent shadow padding. Align visible panels after that flip;
            // transparent shadow pixels may extend beyond the screen edge.
            const int margin = menuShadowMargin();
            const int panelWidth = menu->width() - 2 * margin;
            const auto available = menu->screen()->availableGeometry();
            const int right = parentMenu->geometry().right() - margin + 3;
            const int left = parentMenu->geometry().left() + margin - panelWidth - 2;
            const auto fits = [&](int x) {
                return x >= available.left() && x + panelWidth <= available.right() + 1;
            };
            const bool preferLeft = parentMenu->isRightToLeft();
            int x = preferLeft ? left : right;
            if (!fits(x))
                x = preferLeft ? right : left;
            x = qBound(available.left(), x,
                       qMax(available.left(), available.right() + 1 - panelWidth));
            menu->move(x - margin, menu->y());
        }
    }
}

} // namespace choscordb::design::detail
