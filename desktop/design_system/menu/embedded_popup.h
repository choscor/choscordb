#pragma once

#include "design_system/menu/menu.h"
#include "design_system/style/control_stylesheet.h"
#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointer>
#include <QScopedValueRollback>
#include <QToolButton>
#include <QWidget>

namespace choscordb::design::detail {
// Qt still owns menu actions and combo selection. Only their presentation and
// the outside-click/focus handling formerly provided by a native popup move here.
class EmbeddedPopup final : public QObject {
  public:
    EmbeddedPopup(QWidget* surface, QWidget* origin, bool takeFocus, bool deleteWithOrigin)
        : QObject(surface), surface_(surface) {
        surface->setProperty("embeddedPopupController", QVariant::fromValue<QObject*>(this));
        if (auto* menu = qobject_cast<QMenu*>(surface))
            connect(menu, &QMenu::triggered, this, [this] { dismissChain(); });
        bind(origin, takeFocus, deleteWithOrigin);
        qApp->installEventFilter(this);
    }
    ~EmbeddedPopup() override {
        if (qApp)
            qApp->removeEventFilter(this);
    }
    void bind(QWidget* origin, bool takeFocus, bool deleteWithOrigin) {
        if (origin_ == origin && owner_ == (origin ? origin->window() : nullptr) &&
            takeFocus_ == takeFocus && deleteWithOrigin_ == deleteWithOrigin &&
            surface_->parentWidget() == (origin ? origin->window() : nullptr) &&
            (!origin || !surface_->isWindow()))
            return;
        disconnect(originDestroyed_);
        disconnect(ownerDestroyed_);
        previousFocus_.clear();
        surface_->hide();
        origin_ = origin;
        owner_ = origin ? origin->window() : nullptr;
        takeFocus_ = takeFocus;
        deleteWithOrigin_ = deleteWithOrigin;
        if (origin)
            updateTheme();
        surface_->setProperty("embeddedPopupOwner",
                              origin ? QVariant::fromValue<QObject*>(origin) : QVariant());
        surface_->setParent(owner_, surface_->windowFlags() & ~Qt::WindowType_Mask);
        // Ordinary children auto-show with their parent unless explicitly hidden.
        // A popup must only appear when QMenu/QComboBox/QCompleter requests it.
        surface_->hide();
        if (!origin)
            return;
        originDestroyed_ = connect(origin, &QObject::destroyed, this, [this] {
            const bool remove = deleteWithOrigin_;
            bind(nullptr, takeFocus_, deleteWithOrigin_);
            if (remove)
                surface_->deleteLater();
        });
        if (!deleteWithOrigin && owner_ != origin)
            ownerDestroyed_ = connect(owner_, &QObject::destroyed, this,
                                      [this] { bind(nullptr, takeFocus_, false); });
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (!surface_ || !owner_)
            return false;
        const bool themeChanged =
            event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange ||
            event->type() == QEvent::ApplicationPaletteChange ||
            (event->type() == QEvent::DynamicPropertyChange &&
             static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName() == "designTheme");
        if (themeChanged && origin_) {
            auto* scope = qobject_cast<QWidget*>(watched);
            if (watched == qApp || scope == origin_ || (scope && scope->isAncestorOf(origin_)))
                updateTheme();
        }
        if (watched == surface_ && event->type() == QEvent::Show) {
            if (origin_ && origin_->window() != owner_) {
                const auto requested = surface_->pos();
                bind(origin_, takeFocus_, deleteWithOrigin_);
                surface_->move(requested);
                surface_->show();
                return false;
            }
            updateTheme();
            previousFocus_ = QApplication::focusWidget();
            QPoint requested = owner_->mapFromGlobal(surface_->pos());
            const auto cursor = surface_->property("contextMenuCursor");
            if (cursor.isValid())
                requested = owner_->mapFromGlobal(cursor.toPoint()) -
                            QPoint(menuShadowMargin(), menuShadowMargin());
            else if (auto* button = qobject_cast<QToolButton*>(origin_.data()))
                requested = button->mapTo(
                    owner_, QPoint(-menuShadowMargin(), button->height() + 2 - menuShadowMargin()));
            if (auto* parent = qobject_cast<QMenu*>(origin_.data());
                !cursor.isValid() && parent && parent->isVisible()) {
                const int right = parent->geometry().right() - 2 * menuShadowMargin() + 3;
                const int left = parent->x() + 2 * menuShadowMargin() - surface_->width() - 2;
                int x = parent->isRightToLeft() ? left : right;
                if (x < 0 || x + surface_->width() > owner_->width())
                    x = parent->isRightToLeft() ? right : left;
                requested =
                    QPoint(x, parent->y() + parent->actionGeometry(parent->activeAction()).top() -
                                  menuShadowMargin());
            }
            const QSize size = surface_->size().boundedTo(owner_->size());
            surface_->setMinimumSize(0, 0);
            surface_->resize(size);
            // Keep the visible context-menu panel inside its owner. Transparent
            // shadow padding may be clipped at an edge without shifting the panel
            // away from the invoking cursor.
            const int inset = cursor.isValid() ? menuShadowMargin() : 0;
            surface_->move(qBound(-inset, requested.x(), owner_->width() - size.width() + inset),
                           qBound(-inset, requested.y(), owner_->height() - size.height() + inset));
            surface_->raise();
            if (takeFocus_)
                surface_->setFocus(Qt::PopupFocusReason);
        } else if (watched == surface_ && event->type() == QEvent::Hide) {
            surface_->setProperty("contextMenuCursor", QVariant());
            pointerPressed_ = false;
            pressedAction_.clear();
            if (takeFocus_ && origin_ && owner_->isVisible() && previousFocus_ &&
                previousFocus_->isVisible())
                previousFocus_->setFocus(Qt::PopupFocusReason);
        } else if (surface_->isVisible()) {
            if (auto* menu = qobject_cast<QMenu*>(surface_.data());
                menu && watched == menu &&
                (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress ||
                 event->type() == QEvent::MouseButtonRelease)) {
                auto* mouse = static_cast<QMouseEvent*>(event);
                auto* action = menu->actionAt(mouse->position().toPoint());
                // Native QMenu pointer handling assumes frameGeometry is global.
                // Use local hit testing for a child, retaining Qt's public action
                // selection and keyboard activation (including exec() results).
                if (!action) {
                    if (auto* parent = qobject_cast<QMenu*>(origin_.data())) {
                        const QPoint point =
                            parent->mapFromGlobal(mouse->globalPosition().toPoint());
                        if (parent->actionAt(point)) {
                            QMouseEvent forwarded(mouse->type(), point,
                                                  parent->mapTo(parent->window(), point),
                                                  mouse->globalPosition(), mouse->button(),
                                                  mouse->buttons(), mouse->modifiers());
                            QApplication::sendEvent(parent, &forwarded);
                            return true;
                        }
                    }
                }
                const int margin = menuShadowMargin();
                const auto panel = menu->rect().adjusted(margin, margin, -margin, -margin);
                if (event->type() == QEvent::MouseButtonPress &&
                    !panel.contains(mouse->position().toPoint())) {
                    dismissChain();
                    return true;
                }
                if (action && (!action->isEnabled() || action->isSeparator()))
                    action = nullptr;
                if (event->type() != QEvent::MouseButtonRelease) {
                    for (auto* other : menu->actions())
                        if (other != action && other->menu())
                            other->menu()->hide();
                    menu->setActiveAction(action);
                    if (event->type() == QEvent::MouseButtonPress) {
                        pointerPressed_ = true;
                        pressedAction_ = action;
                    }
                } else {
                    const bool activate = action && menu->activeAction() == action &&
                                          (!pointerPressed_ || pressedAction_ == action);
                    pointerPressed_ = false;
                    pressedAction_.clear();
                    if (activate && !action->menu()) {
                        QKeyEvent activateEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                        QApplication::sendEvent(menu, &activateEvent);
                    }
                }
                return true;
            }
            if (watched == origin_ && event->type() == QEvent::Hide) {
                surface_->hide();
            } else if (watched == owner_ &&
                       (event->type() == QEvent::Resize || event->type() == QEvent::Hide ||
                        event->type() == QEvent::WindowDeactivate)) {
                surface_->hide();
            } else if (event->type() == QEvent::MouseButtonPress) {
                auto* target = qobject_cast<QWidget*>(watched);
                if (!target)
                    return false;
                if (!belongsToPopup(target, surface_)) {
                    if (auto* parentMenu = qobject_cast<QMenu*>(origin_.data());
                        parentMenu && belongsToPopup(target, parentMenu)) {
                        surface_->hide();
                        return false;
                    }
                    dismissChain();
                    return true;
                }
            } else if (event->type() == QEvent::KeyPress &&
                       static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
                auto* target = qobject_cast<QWidget*>(watched);
                if (target == surface_ || (target && surface_->isAncestorOf(target))) {
                    surface_->hide();
                    return true;
                }
            }
        }
        return false;
    }

  private:
    void updateTheme() {
        if (!origin_ || updatingTheme_)
            return;
        const QScopedValueRollback<bool> updating(updatingTheme_, true);
        const auto theme = resolvedThemeForWidget(*origin_);
        surface_->setProperty("designTheme", QVariant::fromValue(theme));
        auto palette = applicationPalette(theme);
        if (qobject_cast<QMenu*>(surface_)) {
            palette.setColor(QPalette::Window, theme.colors.popover);
            const auto sheet = controlStyleSheet(theme);
            if (surface_->styleSheet() != sheet)
                surface_->setStyleSheet(sheet);
        }
        surface_->setPalette(palette);
    }
    static bool belongsToPopup(QWidget* target, QWidget* popup) {
        for (int depth = 0; target && depth < 64; ++depth) {
            if (target == popup || popup->isAncestorOf(target))
                return true;
            QWidget* origin = nullptr;
            for (auto* widget = target; widget && !origin; widget = widget->parentWidget())
                origin = qobject_cast<QWidget*>(
                    widget->property("embeddedPopupOwner").value<QObject*>());
            target = origin;
        }
        return false;
    }
    void dismissChain() {
        QPointer<QWidget> current = surface_;
        while (current) {
            QPointer<QWidget> origin =
                qobject_cast<QWidget*>(current->property("embeddedPopupOwner").value<QObject*>());
            current->hide();
            current = qobject_cast<QMenu*>(origin.data());
        }
    }
    QPointer<QWidget> surface_, origin_, owner_, previousFocus_;
    QMetaObject::Connection originDestroyed_, ownerDestroyed_;
    QPointer<QAction> pressedAction_;
    bool pointerPressed_ = false;
    bool updatingTheme_ = false;
    bool takeFocus_ = true;
    bool deleteWithOrigin_ = true;
};
inline void embedPopup(QWidget* popup, QWidget* origin, bool takeFocus = true,
                       bool deleteWithOrigin = true) {
    if (auto* controller = dynamic_cast<EmbeddedPopup*>(
            popup->property("embeddedPopupController").value<QObject*>())) {
        controller->bind(origin, takeFocus, deleteWithOrigin);
        return;
    }
    for (auto* ancestor = origin; ancestor; ancestor = ancestor->parentWidget())
        if (auto* bar = qobject_cast<QMenuBar*>(ancestor); bar && bar->isNativeMenuBar())
            return;
    if (origin)
        new EmbeddedPopup(popup, origin, takeFocus, deleteWithOrigin);
}

} // namespace choscordb::design::detail

namespace choscordb::design::detail {
inline QWidget* activeEmbeddedPopup() {
    if (auto* native = QApplication::activePopupWidget())
        return native;
    for (auto* widget = QApplication::focusWidget(); widget; widget = widget->parentWidget())
        if (widget->isVisible() && widget->property("embeddedPopupOwner").isValid() &&
            widget->objectName() != "designTooltip")
            return widget;
    for (auto* widget : QApplication::allWidgets())
        if (widget->isVisible() && widget->property("embeddedPopupOwner").isValid() &&
            widget->objectName() != "designTooltip")
            return widget;
    return nullptr;
}
} // namespace choscordb::design::detail
