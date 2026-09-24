#pragma once
#include "design_system/icons.h"
#include "design_system/theme_manager.h"
#include "models/navigator_model.h"
#include <QApplication>
#include <QEvent>
#include <QIcon>
#include <QList>
#include <QMouseEvent>
#include <QPointer>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <functional>
#include <utility>

namespace choscordb::main_window_detail {
class NavigatorIconDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    std::function<design::Icon(quint64)> connectionIcon;
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        const auto kind = index.data(NavigatorModel::KindRole).toString();
        if (kind == "group") {
            option->icon = QIcon();
            option->features &= ~QStyleOptionViewItem::HasDecoration;
            return;
        }
        const auto role =
            kind == "connection"
                ? (connectionIcon
                       ? connectionIcon(index.data(NavigatorModel::ConnectionRole).toULongLong())
                       : design::Icon::Database)
            : kind == "schema" || kind == "database"  ? design::Icon::Folder
            : kind == "table" || kind == "view"       ? design::Icon::Table
            : kind == "index" || kind.contains("key") ? design::Icon::Key
                                                      : design::Icon::File;
        if (option->widget) {
            const auto colors = design::resolvedThemeForWidget(*option->widget).colors;
            option->icon = design::themedIcon(role, colors.mutedText, 14);
            option->features |= QStyleOptionViewItem::HasDecoration;
            option->decorationSize = QSize(14, 14);
        }
    }
};

class ContextualActionVisibility final : public QObject {
  public:
    ContextualActionVisibility(QWidget* region, QList<QWidget*> actions)
        : QObject(region), region_(region), actions_(std::move(actions)) {
        region_->installEventFilter(this);
        connect(qApp, &QApplication::focusChanged, this,
                [this](QWidget*, QWidget*) { updateForCurrentInput(); });
        setActionsVisible(false);
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == region_) {
            if (event->type() == QEvent::Enter) {
                setActionsVisible(true);
            } else if (event->type() == QEvent::Leave) {
                QTimer::singleShot(0, this, [this] { updateForCurrentInput(); });
            }
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    void updateForCurrentInput() {
        auto* focused = QApplication::focusWidget();
        setActionsVisible(region_->underMouse() || focused == region_ ||
                          (focused != nullptr && region_->isAncestorOf(focused)));
    }

    void setActionsVisible(bool visible) {
        for (auto* action : actions_) {
            action->setVisible(visible);
        }
    }

    QWidget* region_;
    QList<QWidget*> actions_;
};

class HoveredTabCloseVisibility final : public QObject {
  public:
    explicit HoveredTabCloseVisibility(QTabBar* tabs) : QObject(tabs), tabs_(tabs) {
        tabs_->setMouseTracking(true);
        tabs_->installEventFilter(this);
        connect(tabs_, &QTabBar::currentChanged, this, [this] { updateButtons(-1); });
        QTimer::singleShot(0, this, [this] { updateButtons(-1); });
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != tabs_) {
            return QObject::eventFilter(watched, event);
        }
        if (event->type() == QEvent::MouseMove) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            updateButtons(tabs_->tabAt(mouse->position().toPoint()));
        } else if (event->type() == QEvent::Leave) {
            updateButtons(-1);
        } else if (event->type() == QEvent::ChildAdded || event->type() == QEvent::LayoutRequest) {
            QTimer::singleShot(0, this, [this] { updateButtons(-1); });
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    void updateButtons(int hovered) {
        const auto side = static_cast<QTabBar::ButtonPosition>(
            tabs_->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, tabs_));
        for (int index = 0; index < tabs_->count(); ++index) {
            if (auto* button = tabs_->tabButton(index, side)) {
                button->setVisible(index == tabs_->currentIndex() || index == hovered);
            }
        }
    }

    QTabBar* tabs_;
};

class WorkspaceTabBar final : public QTabBar {
  public:
    using QTabBar::QTabBar;

    void setHeader(QWidget* header) {
        header_ = header;
        header_->setParent(this);
        header_->show();
        updateGeometry();
        layoutHeader();
    }

    QSize sizeHint() const override {
        auto size = QTabBar::sizeHint();
        if (header_ && !header_->isHidden())
            size.rheight() += header_->sizeHint().height() + design::spacing(design::Spacing::Half);
        return size;
    }

    QSize minimumSizeHint() const override {
        auto size = QTabBar::minimumSizeHint();
        if (header_ && !header_->isHidden())
            size.rheight() +=
                header_->minimumSizeHint().height() + design::spacing(design::Spacing::Half);
        return size;
    }

    void setHeaderVisible(bool visible) {
        if (!header_ || header_->isHidden() != visible)
            return;
        header_->setVisible(visible);
        updateGeometry();
        layoutHeader();
    }

  protected:
    void resizeEvent(QResizeEvent* event) override {
        QTabBar::resizeEvent(event);
        layoutHeader();
    }

  private:
    void layoutHeader() {
        if (header_ && !header_->isHidden())
            header_->setGeometry(0, QTabBar::sizeHint().height(), width(),
                                 header_->sizeHint().height());
    }
    QPointer<QWidget> header_;
};

class WorkspaceTabs final : public QTabWidget {
  public:
    WorkspaceTabs() {
        setTabBar(new WorkspaceTabBar(this));
        tabBar()->installEventFilter(this);
        connect(tabBar(), &QTabBar::tabMoved, this, [this] { scheduleAddButton(); });
        connect(tabBar(), &QTabBar::currentChanged, this, [this] { scheduleAddButton(); });
    }
    WorkspaceTabBar* workspaceBar() const { return static_cast<WorkspaceTabBar*>(tabBar()); }
    void setAddButton(QWidget* button) {
        addButton_ = button;
        setCornerWidget(button, Qt::TopRightCorner);
        scheduleAddButton();
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == tabBar() &&
            (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest))
            scheduleAddButton();
        return QTabWidget::eventFilter(watched, event);
    }
    void resizeEvent(QResizeEvent* event) override {
        QTabWidget::resizeEvent(event);
        scheduleAddButton();
    }

  private:
    void scheduleAddButton() {
        QTimer::singleShot(0, this, [this] { layoutAddButton(); });
    }
    void layoutAddButton() {
        if (!addButton_ || !tabBar()->isVisible())
            return;
        const int right = tabBar()->mapTo(this, QPoint(tabBar()->width(), 0)).x();
        const auto last = tabBar()->count() ? tabBar()->tabRect(tabBar()->count() - 1) : QRect{};
        const bool overflow = tabBar()->count() && (tabBar()->tabRect(0).left() < 0 ||
                                                    last.right() >= tabBar()->width());
        const int end = tabBar()->mapTo(this, tabBar()->count() ? last.topRight() : QPoint()).x();
        const int width = addButton_->width();
        const int corner = qMin(right + 2, this->width() - width);
        const int x = !overflow && end + 4 + width <= right ? end + 4 : corner;
        addButton_->move(x, addButton_->y());
    }
    QPointer<QWidget> addButton_;
};

} // namespace choscordb::main_window_detail
