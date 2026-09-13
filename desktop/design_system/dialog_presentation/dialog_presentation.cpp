#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/theme.h"
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QGraphicsBlurEffect>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScopedValueRollback>
#include <QTimer>

namespace choscordb::design {
namespace {
class Backdrop final : public QWidget {
  public:
    Backdrop(QWidget* parent, QDialog& dialog) : QWidget(parent), dialog_(dialog) {
        setObjectName("modalBackdrop");
        setFocusPolicy(Qt::NoFocus);
    }

    void captureOwner() {
        // grab() delivers pending Resize events for hidden owners. Those events
        // request a recapture; the outer grab already observes their new size.
        if (capturing_)
            return;
        const QScopedValueRollback<bool> captureGuard(capturing_, true);
        const bool visible = isVisible();
        hide();
        const auto source = parentWidget()->grab();
        background_ = QPixmap(source.size());
        background_.setDevicePixelRatio(source.devicePixelRatio());
        background_.fill(Qt::transparent);
        QGraphicsScene scene;
        auto* item = scene.addPixmap(source);
        auto* effect = new QGraphicsBlurEffect;
        effect->setBlurRadius(backdropBlurRadius() * 2);
        item->setGraphicsEffect(effect);
        QPainter painter(&background_);
        const QRectF bounds(QPointF(), source.deviceIndependentSize());
        scene.render(&painter, bounds, bounds);
        if (visible)
            show();
    }

    void scheduleCapture() {
        if (capturePending_)
            return;
        capturePending_ = true;
        // Style/palette filters run before child widgets receive their new
        // palette. Capture after that propagation, coalescing the event burst.
        QTimer::singleShot(0, this, [this] {
            capturePending_ = false;
            if (isVisible()) {
                captureOwner();
                update();
            }
        });
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const auto theme = resolvedThemeForWidget(dialog_);
        if (!theme.forcedContrast)
            painter.drawPixmap(0, 0, background_);
        painter.fillRect(rect(), theme.colors.backdrop);
        if (theme.forcedContrast)
            return;
        const QRect panel(parentWidget()->mapFromGlobal(dialog_.mapToGlobal(QPoint())),
                          dialog_.size());
        if (shadow_.isNull() || panel != shadowPanel_ || size() != shadowSize_ ||
            devicePixelRatioF() != shadowScale_) {
            shadowPanel_ = panel;
            shadowSize_ = size();
            shadowScale_ = devicePixelRatioF();
            shadow_ = QPixmap((QSizeF(size()) * shadowScale_).toSize());
            shadow_.setDevicePixelRatio(shadowScale_);
            shadow_.fill(Qt::transparent);
            QPainter shadowPainter(&shadow_);
            QPainterPath path;
            path.addRoundedRect(panel, radius(Radius::ExtraLarge), radius(Radius::ExtraLarge));
            QPainterPath outside;
            outside.addRect(rect());
            // QGraphicsDropShadowEffect includes its source in the scene render.
            // Keep only the exterior shadow; native window transparency must
            // expose the dimmed owner, never the black source silhouette.
            shadowPainter.setClipPath(outside.subtracted(path));
            for (const auto& layer : elevation(Elevation::Dialog)) {
                QGraphicsScene scene;
                auto* item = scene.addPath(path, Qt::NoPen, Qt::black);
                auto* effect = new QGraphicsDropShadowEffect;
                effect->setBlurRadius(layer.blur * 2);
                effect->setOffset(layer.x, layer.y);
                auto color = layer.color;
                color.setAlphaF(layer.opacity);
                effect->setColor(color);
                item->setGraphicsEffect(effect);
                scene.render(&shadowPainter, rect(), rect());
            }
        }
        painter.drawPixmap(0, 0, shadow_);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            dialog_.reject();
            event->accept();
        }
    }

  private:
    QDialog& dialog_;
    bool capturing_ = false;
    bool capturePending_ = false;
    QPixmap background_;
    QPixmap shadow_;
    QRect shadowPanel_;
    QSize shadowSize_;
    qreal shadowScale_ = 0;
};
} // namespace
DialogPresentation::DialogPresentation(QDialog& dialog) : QObject(&dialog), dialog_(dialog) {
    dialog_.installEventFilter(this);
}
DialogPresentation::~DialogPresentation() {
    delete backdrop_.data();
}
void DialogPresentation::center() {
    if (!owner_) {
        return;
    }
    if (backdrop_) {
        backdrop_->setGeometry(owner_->rect());
    }
    dialog_.move(owner_->mapToGlobal(owner_->rect().center()) - dialog_.rect().center());
    if (backdrop_)
        backdrop_->update();
}
void DialogPresentation::shown() {
    if (!dialog_.isModal()) {
        return;
    }
    auto* parent = dialog_.parentWidget();
    auto* owner = parent ? parent->window() : nullptr;
    if (owner_ != owner) {
        if (owner_) {
            owner_->removeEventFilter(this);
        }
        delete backdrop_.data();
        owner_ = owner;
        if (owner_) {
            owner_->installEventFilter(this);
        }
    }
    previousFocus_ = owner_ ? owner_->focusWidget() : QApplication::focusWidget();
    if (owner_) {
        if (!backdrop_) {
            backdrop_ = new Backdrop(owner_, dialog_);
        }
        static_cast<Backdrop*>(backdrop_.data())->captureOwner();
        backdrop_->show();
        backdrop_->raise();
        center();
    }
}
void DialogPresentation::hidden() {
    if (backdrop_) {
        backdrop_->hide();
    }
    if (previousFocus_ && previousFocus_->isVisible() && previousFocus_->isEnabled()) {
        previousFocus_->window()->activateWindow();
        previousFocus_->setFocus(Qt::OtherFocusReason);
    }
    previousFocus_.clear();
}
bool DialogPresentation::eventFilter(QObject* watched, QEvent* event) {
    if (watched == &dialog_ && dialog_.isVisible() && dialog_.isModal() &&
        event->type() == QEvent::Resize) {
        center();
    }
    if (watched == owner_ && dialog_.isVisible() && dialog_.isModal() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move ||
         event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)) {
        if (event->type() == QEvent::Resize) {
            QEvent request(QEvent::LayoutRequest);
            QApplication::sendEvent(&dialog_, &request);
            if (backdrop_)
                static_cast<Backdrop*>(backdrop_.data())->scheduleCapture();
        }
        if (backdrop_ &&
            (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange))
            static_cast<Backdrop*>(backdrop_.data())->scheduleCapture();
        center();
    }
    return QObject::eventFilter(watched, event);
}
void paintDialogSurface(QWidget& widget) {
    QPainter painter(&widget);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto theme = resolvedThemeForWidget(widget);
    const auto& colors = theme.colors;
    painter.setBrush(colors.popover);
    const auto border = colors.border;
    painter.setPen(QPen(border, 1));
    const auto cornerRadius = resolveMetrics(Density::Compact, true).dialogRadius;
    painter.drawRoundedRect(QRectF(widget.rect()).adjusted(.5, .5, -.5, -.5), cornerRadius,
                            cornerRadius);
}
} // namespace choscordb::design
