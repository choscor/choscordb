#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/theme.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QGraphicsBlurEffect>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScopedValueRollback>
#include <QSet>
#include <QTimer>

namespace choscordb::design {
namespace {
QList<QPointer<QDialog>> activeDialogs;
bool belongsToDialog(QWidget* widget, const QDialog& dialog) {
    QSet<QWidget*> visited;
    while (widget && !visited.contains(widget)) {
        if (widget == &dialog || dialog.isAncestorOf(widget))
            return true;
        visited.insert(widget);
        auto* origin =
            qobject_cast<QWidget*>(widget->property("embeddedPopupOwner").value<QObject*>());
        widget = origin ? origin : widget->parentWidget();
    }
    return false;
}
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
        // Rendering must omit this layer and any modal above it. Changing
        // visibility attributes avoids hide/show lifecycle signals during grab.
        QList<QWidget*> excluded;
        const int layer = activeDialogs.indexOf(&dialog_);
        for (int i = qMax(0, layer); i < activeDialogs.size(); ++i) {
            if (auto* modal = activeDialogs[i].data(); modal && modal->window() == dialog_.window())
                excluded.append(modal);
        }
        for (auto* child :
             parentWidget()->findChildren<QWidget*>(QString{}, Qt::FindDirectChildrenOnly)) {
            if (auto* backdrop = dynamic_cast<Backdrop*>(child);
                backdrop && activeDialogs.indexOf(&backdrop->dialog_) >= layer)
                excluded.append(backdrop);
        }
        QList<QWidget*> visible;
        for (auto* widget : excluded) {
            if (widget->isVisible()) {
                visible.append(widget);
                widget->setAttribute(Qt::WA_WState_Visible, false);
                widget->setAttribute(Qt::WA_WState_Hidden, true);
            }
        }
        const auto source = parentWidget()->grab();
        for (auto* widget : visible) {
            widget->setAttribute(Qt::WA_WState_Hidden, false);
            widget->setAttribute(Qt::WA_WState_Visible, true);
        }
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
    }

    void scheduleCapture() {
        if (capturePending_)
            return;
        capturePending_ = true;
        // Style/palette filters run before child widgets receive their new
        // palette. Capture after that propagation, coalescing the event burst.
        QTimer::singleShot(0, this, [this] {
            if (!capturePending_)
                return;
            // Nested layers must capture the newly rendered lower backdrop,
            // even when their palette event arrived first.
            const auto siblings =
                parentWidget()->findChildren<QWidget*>(QString{}, Qt::FindDirectChildrenOnly);
            for (const auto& dialog : activeDialogs) {
                for (auto* sibling : siblings) {
                    auto* backdrop = dynamic_cast<Backdrop*>(sibling);
                    if (backdrop && &backdrop->dialog_ == dialog && backdrop->capturePending_) {
                        backdrop->capturePending_ = false;
                        if (backdrop->isVisible()) {
                            backdrop->captureOwner();
                            backdrop->update();
                        }
                    }
                }
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
            // Keep only the exterior shadow so the rounded surface exposes
            // the dimmed owner, never the black source silhouette.
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
DialogPresentation::DialogPresentation(QDialog& dialog) : QObject(&dialog), dialog_(dialog) {}
DialogPresentation::~DialogPresentation() {
    activeDialogs.removeAll(&dialog_);
    delete backdrop_.data();
}
void DialogPresentation::makeModal() {
    auto* parent = dialog_.parentWidget();
    if (parent) {
        anchor_ = parent;
        if (parent != parent->window()) {
            // Embedding changes visual parenting, but not the originating
            // widget's ownership of the dialog's lifetime.
            connect(parent, &QObject::destroyed, &dialog_, &QObject::deleteLater);
        }
        dialog_.setParent(parent->window(), Qt::Widget | Qt::FramelessWindowHint);
        // A newly embedded child must not be implicitly shown with its owner.
        dialog_.hide();
        dialog_.setWindowModality(Qt::NonModal);
    } else {
        dialog_.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        dialog_.setWindowModality(Qt::ApplicationModal);
    }
    dialog_.setAttribute(Qt::WA_TranslucentBackground);
    dialog_.setProperty("embeddedModal", parent != nullptr);
}
QDialog* DialogPresentation::activeDialog(QWidget* owner) {
    for (auto i = activeDialogs.crbegin(); i != activeDialogs.crend(); ++i) {
        if (*i && (*i)->isVisible() && (!owner || (*i)->window() == owner->window()))
            return *i;
    }
    return QApplication::activeModalWidget()
               ? qobject_cast<QDialog*>(QApplication::activeModalWidget())
               : nullptr;
}
void DialogPresentation::center() {
    if (!owner_) {
        return;
    }
    if (backdrop_) {
        backdrop_->setGeometry(owner_->rect());
    }
    if (!dialog_.isWindow()) {
        const auto available = (owner_->size() - QSize(32, 32)).expandedTo(QSize(1, 1));
        dialog_.setMaximumSize(available);
        dialog_.resize(dialog_.size().boundedTo(available));
    }
    dialog_.move((dialog_.isWindow() ? owner_->mapToGlobal(owner_->rect().center())
                                     : owner_->rect().center()) -
                 dialog_.rect().center());
    if (backdrop_)
        backdrop_->update();
}
void DialogPresentation::scheduleCenter() {
    if (centerPending_)
        return;
    centerPending_ = true;
    QTimer::singleShot(0, this, [this] {
        centerPending_ = false;
        if (dialog_.isVisible() && active_)
            center();
    });
}
void DialogPresentation::shown() {
    if (!dialog_.isModal() && !dialog_.property("embeddedModal").toBool()) {
        return;
    }
    auto* parent = dialog_.parentWidget();
    auto* owner = parent ? parent->window() : nullptr;
    if (owner_ != owner) {
        delete backdrop_.data();
        owner_ = owner;
    }
    if (active_)
        return;
    previousFocus_ = owner_ ? owner_->focusWidget() : QApplication::focusWidget();
    active_ = true;
    activeDialogs.append(&dialog_);
    qApp->installEventFilter(this);
    if (owner_) {
        if (!backdrop_) {
            backdrop_ = new Backdrop(owner_, dialog_);
        }
        static_cast<Backdrop*>(backdrop_.data())->captureOwner();
        backdrop_->show();
        backdrop_->raise();
        center();
        dialog_.raise();
        auto* focus = dialog_.focusWidget();
        if (!focus) {
            for (auto* candidate : dialog_.findChildren<QWidget*>()) {
                if (candidate->isVisible() && candidate->isEnabled() &&
                    candidate->focusPolicy() & Qt::TabFocus) {
                    focus = candidate;
                    break;
                }
            }
        }
        (focus ? focus : &dialog_)->setFocus(Qt::OtherFocusReason);
    }
}
void DialogPresentation::hidden() {
    if (!active_)
        return;
    active_ = false;
    activeDialogs.removeAll(&dialog_);
    qApp->removeEventFilter(this);
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
    if (active_ && anchor_ && anchor_ != owner_ && watched == anchor_ &&
        event->type() == QEvent::Hide) {
        dialog_.reject();
        return false;
    }
    if (active_ && !dialog_.isWindow() && activeDialog(owner_) == &dialog_) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (event->type() == QEvent::Shortcut && !widget) {
            widget = qobject_cast<QWidget*>(watched->parent());
            if (auto* action = qobject_cast<QAction*>(watched)) {
                for (auto* associated : action->associatedObjects()) {
                    if (auto* associatedWidget = qobject_cast<QWidget*>(associated);
                        associatedWidget && associatedWidget->window() == owner_) {
                        widget = associatedWidget;
                        break;
                    }
                }
            }
        }
        const bool inside = belongsToDialog(widget, dialog_);
        const bool sameWindow = widget && widget->window() == owner_;
        if (sameWindow && !inside && widget != backdrop_) {
            switch (event->type()) {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease:
            case QEvent::MouseButtonDblClick:
            case QEvent::Wheel:
            case QEvent::KeyPress:
            case QEvent::KeyRelease:
            case QEvent::Shortcut:
                return true;
            case QEvent::FocusIn:
                if (auto* focus = dialog_.focusWidget())
                    focus->setFocus();
                else
                    dialog_.setFocus();
                break;
            default:
                break;
            }
        }
        if (widget && (widget == &dialog_ || dialog_.isAncestorOf(widget)) &&
            event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
                const bool backwards =
                    key->key() == Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier);
                auto* start = QApplication::focusWidget();
                auto* next = start ? start : &dialog_;
                do {
                    next = backwards ? next->previousInFocusChain() : next->nextInFocusChain();
                    if (dialog_.isAncestorOf(next) && next->isVisible() && next->isEnabled() &&
                        (next->focusPolicy() & Qt::TabFocus)) {
                        next->setFocus(backwards ? Qt::BacktabFocusReason : Qt::TabFocusReason);
                        break;
                    }
                } while (next != (start ? start : &dialog_));
                return true;
            }
        }
    }
    if (watched == &dialog_ && dialog_.isVisible() && active_ && event->type() == QEvent::Resize) {
        center();
    }
    if (watched == &dialog_ && backdrop_ && dialog_.isVisible() && active_ &&
        event->type() == QEvent::UpdateRequest) {
        backdrop_->update();
    }
    if (watched == owner_ && dialog_.isVisible() && active_ &&
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
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move)
            scheduleCenter();
    }
    return QObject::eventFilter(watched, event);
}
void paintDialogSurface(QWidget& widget, bool drawBorder) {
    QPainter painter(&widget);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto theme = resolvedThemeForWidget(widget);
    const auto& colors = theme.colors;
    painter.setBrush(colors.popover);
    painter.setPen(drawBorder ? QPen(colors.border, 1) : Qt::NoPen);
    const auto cornerRadius = resolveMetrics(Density::Compact, true).dialogRadius;
    const auto bounds =
        drawBorder ? QRectF(widget.rect()).adjusted(.5, .5, -.5, -.5) : QRectF(widget.rect());
    painter.drawRoundedRect(bounds, cornerRadius, cornerRadius);
}
} // namespace choscordb::design
