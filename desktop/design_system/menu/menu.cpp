#include "design_system/menu/menu.h"
#include "design_system/menu/embedded_popup.h"
#include "design_system/theme.h"
#include <QEvent>
#include <QGraphicsEffect>
#include <QGuiApplication>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QToolButton>
#include <vector>

namespace choscordb::design::detail {
namespace {
// Blur only the source alpha. Drawing a QGraphicsDropShadowEffect through a scene
// also paints its solid source silhouette, which leaves dark corner artifacts.
void blurAlpha(std::vector<int>& alpha, int width, int height, int radius) {
    if (radius <= 0)
        return;
    std::vector<int> pass(alpha.size());
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (int y = 0; y < height; ++y) {
            int sum = 0;
            for (int x = 0; x < width + radius; ++x) {
                if (x < width)
                    sum += alpha[y * width + x];
                if (x >= 2 * radius + 1)
                    sum -= alpha[y * width + x - (2 * radius + 1)];
                if (x >= radius && x - radius < width)
                    pass[y * width + x - radius] = sum / (2 * radius + 1);
            }
        }
        for (int x = 0; x < width; ++x) {
            int sum = 0;
            for (int y = 0; y < height + radius; ++y) {
                if (y < height)
                    sum += pass[y * width + x];
                if (y >= 2 * radius + 1)
                    sum -= pass[(y - (2 * radius + 1)) * width + x];
                if (y >= radius && y - radius < height)
                    alpha[(y - radius) * width + x] = sum / (2 * radius + 1);
            }
        }
    }
}
} // namespace
int menuShadowMargin() {
    int margin = 0;
    for (const auto& layer : elevation(Elevation::Medium))
        margin = qMax(margin,
                      layer.blur * 2 + qMax(qAbs(layer.x), qAbs(layer.y)) + qMax(0, layer.spread));
    return margin;
}
QPoint contextMenuPosition(const QPoint& cursor) {
    const int margin = menuShadowMargin();
    QPoint position = cursor - QPoint(margin, margin);
    // Keep the popup window on screen when its shadow would extend past an edge.
    if (const auto* screen = QGuiApplication::screenAt(cursor)) {
        const QRect available = screen->availableGeometry();
        position.setX(qMax(position.x(), available.left()));
        position.setY(qMax(position.y(), available.top()));
    }
    return position;
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
            const QImage sourceImage = source.toImage();
            const int width = sourceImage.width();
            const int height = sourceImage.height();
            QImage shadowImage(sourceImage.size(), QImage::Format_ARGB32_Premultiplied);
            shadowImage.fill(Qt::transparent);
            const qreal scale = source.devicePixelRatio();
            for (const auto& layer : elevation(Elevation::Medium)) {
                std::vector<int> alpha(width * height);
                const int dx = qRound(layer.x * scale);
                const int dy = qRound(layer.y * scale);
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x) {
                        const int sx = x - dx;
                        const int sy = y - dy;
                        if (sx >= 0 && sx < width && sy >= 0 && sy < height)
                            alpha[y * width + x] = sourceImage.pixelColor(sx, sy).alpha();
                    }
                blurAlpha(alpha, width, height, qMax(1, qRound(layer.blur * scale / 2)));
                QImage colored(sourceImage.size(), QImage::Format_ARGB32_Premultiplied);
                const auto color = layer.color;
                for (int y = 0; y < height; ++y) {
                    auto* row = reinterpret_cast<QRgb*>(colored.scanLine(y));
                    for (int x = 0; x < width; ++x) {
                        const int opacity = qRound(alpha[y * width + x] * layer.opacity);
                        row[x] = qRgba(color.red() * opacity / 255, color.green() * opacity / 255,
                                       color.blue() * opacity / 255, opacity);
                    }
                }
                QPainter combined(&shadowImage);
                combined.drawImage(0, 0, colored);
            }
            shadow_ = QPixmap::fromImage(shadowImage);
            shadow_.setDevicePixelRatio(scale);
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
        auto* origin = widget->parentWidget();
        widget->setWindowFlag(Qt::NoDropShadowWindowHint);
        widget->setAttribute(Qt::WA_TranslucentBackground);
        widget->setGraphicsEffect(new MenuShadowEffect(widget));
        embedPopup(widget, origin);
    }
}
void positionSubmenu(QWidget* field, QEvent* event) {
    if (auto* menu = qobject_cast<QMenu*>(field); menu && event->type() == QEvent::Show) {
        if (menu->property("embeddedPopupOwner").isValid())
            return;
        if (auto* button = qobject_cast<QToolButton*>(menu->parentWidget());
            button && button->menu() == menu) {
            const int margin = menuShadowMargin();
            const auto available = menu->screen()->availableGeometry();
            const int panelWidth = menu->width() - 2 * margin;
            const int panelHeight = menu->height() - 2 * margin;
            const QPoint anchor = button->mapToGlobal(QPoint(0, button->height() + 2));
            const int x = qBound(available.left(), anchor.x(),
                                 qMax(available.left(), available.right() + 1 - panelWidth));
            const int y = qBound(available.top(), anchor.y(),
                                 qMax(available.top(), available.bottom() + 1 - panelHeight));
            menu->move(x - margin, y - margin);
            return;
        }
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
