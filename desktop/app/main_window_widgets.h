#pragma once
#include "app/object_kind_icon.h"
#include "design_system/column_row/column_row.h"
#include "design_system/icons.h"
#include "design_system/tabs/tab_add_corner.h"
#include "design_system/tabs/tabs_style.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include "models/navigator_model.h"
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QList>
#include <QListWidget>
#include <QMouseEvent>
#include <QPalette>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <functional>
#include <utility>

namespace choscordb::main_window_detail {
// Centered placeholder text shared by the start page and sidebar empty states.
inline design::Text* createEmptyStateText(const QString& text, QWidget* parent) {
    auto* label = new design::Text(text, parent);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setAlignment(Qt::AlignCenter);
    label->setMargin(design::spacing(design::Spacing::Three));
    label->setTextFormat(Qt::PlainText);
    return label;
}

class NavigatorIconDelegate final : public design::ColumnRowDelegate {
  public:
    explicit NavigatorIconDelegate(QObject* parent = nullptr)
        : design::ColumnRowDelegate(NavigatorModel::DatabaseTypeRole, parent) {}
    std::function<design::Icon(quint64)> connectionIcon;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        if (index.data(NavigatorModel::KindRole).toString() == QLatin1String("column")) {
            design::ColumnRowDelegate::paint(painter, option, index);
            return;
        }
        QStyledItemDelegate::paint(painter, option, index);
    }
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        const auto kind = index.data(NavigatorModel::KindRole).toString();
        if (kind == "group" || kind == "column") {
            option->icon = QIcon();
            option->features &= ~QStyleOptionViewItem::HasDecoration;
            return;
        }
        const auto role =
            kind == "connection"
                ? (connectionIcon
                       ? connectionIcon(index.data(NavigatorModel::ConnectionRole).toULongLong())
                       : design::Icon::Database)
                : objectKindIcon(kind);
        if (option->widget) {
            const auto colors = design::resolvedThemeForWidget(*option->widget).colors;
            const int size = objectIconSize();
            option->icon = design::themedIcon(role, colors.mutedText, size);
            option->features |= QStyleOptionViewItem::HasDecoration;
            option->decorationSize = QSize(size, size);
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

class SidebarWheelForwarder final : public QObject {
  public:
    SidebarWheelForwarder(QScrollArea* scroll, QWidget* source) : QObject(scroll), scroll_(scroll) {
        source->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() != QEvent::Wheel)
            return QObject::eventFilter(watched, event);
        auto* wheel = static_cast<QWheelEvent*>(event);
        if (wheel->angleDelta().y() == 0 && wheel->pixelDelta().y() == 0)
            return QObject::eventFilter(watched, event);
        auto* viewport = scroll_->viewport();
        QWheelEvent forwarded(viewport->mapFromGlobal(wheel->globalPosition().toPoint()),
                              wheel->globalPosition(), wheel->pixelDelta(), wheel->angleDelta(),
                              wheel->buttons(), wheel->modifiers(), wheel->phase(),
                              wheel->inverted(), wheel->source(), wheel->pointingDevice());
        QCoreApplication::sendEvent(viewport, &forwarded);
        wheel->setAccepted(forwarded.isAccepted());
        return true;
    }

  private:
    QScrollArea* scroll_;
};

class SidebarWidthObserver final : public QObject {
  public:
    SidebarWidthObserver(QWidget* viewport, std::function<void()> widthChanged)
        : QObject(viewport), widthChanged_(std::move(widthChanged)) {
        viewport->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Resize) {
            const auto* resize = static_cast<QResizeEvent*>(event);
            if (resize->size().width() != resize->oldSize().width())
                QTimer::singleShot(0, this, widthChanged_);
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    std::function<void()> widthChanged_;
};

class SidebarConnectionListScroll final : public QObject {
  public:
    SidebarConnectionListScroll(QListWidget* list, QScrollArea* scroll, design::ThemeManager* theme)
        : QObject(list), list_(list), scroll_(scroll) {
        list->installEventFilter(this);
        const auto updateHeight = [list] {
            int height = 0;
            for (int row = 0; row < list->count(); ++row)
                height += list->sizeHintForRow(row) + 2 * list->spacing();
            list->setFixedHeight(list->count() == 0 ? 0 : height + 2 * list->frameWidth());
        };
        const auto scheduleHeight = [this, updateHeight] {
            QTimer::singleShot(0, this, updateHeight);
        };
        auto* model = list->model();
        connect(model, &QAbstractItemModel::rowsInserted, this, scheduleHeight);
        connect(model, &QAbstractItemModel::rowsRemoved, this, scheduleHeight);
        connect(model, &QAbstractItemModel::modelReset, this, scheduleHeight);
        connect(model, &QAbstractItemModel::layoutChanged, this, scheduleHeight);
        connect(model, &QAbstractItemModel::dataChanged, this, scheduleHeight);
        connect(theme, &design::ThemeManager::metricsChanged, this, scheduleHeight);
        scheduleHeight();
        const auto revealCurrent = [list, scroll] {
            auto* item = list->currentItem();
            if (!item)
                return;
            const auto row = list->visualItemRect(item);
            const auto point = list->viewport()->mapTo(scroll->widget(), row.center());
            scroll->ensureVisible(point.x(), point.y(), 0, row.height());
        };
        connect(list, &QListWidget::currentItemChanged, this,
                [this, revealCurrent] { QTimer::singleShot(0, this, revealCurrent); });
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != list_ || event->type() != QEvent::KeyPress || !list_->currentItem())
            return QObject::eventFilter(watched, event);
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() != Qt::Key_PageDown && key->key() != Qt::Key_PageUp)
            return QObject::eventFilter(watched, event);
        const int direction = key->key() == Qt::Key_PageDown ? 1 : -1;
        const auto current = list_->visualItemRect(list_->currentItem());
        const int distance =
            std::max(current.height(), scroll_->viewport()->height() - current.height());
        const int targetY = current.center().y() + direction * distance;
        int targetRow = list_->currentRow();
        for (int row = targetRow + direction; row >= 0 && row < list_->count(); row += direction) {
            const int centerY = list_->visualItemRect(list_->item(row)).center().y();
            if (direction * (centerY - targetY) > 0)
                break;
            targetRow = row;
        }
        // MultiSelection page keys move focus without changing the selected connections.
        list_->setCurrentRow(targetRow, QItemSelectionModel::NoUpdate);
        key->accept();
        return true;
    }

  private:
    QListWidget* list_;
    QScrollArea* scroll_;
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

class WorkspaceTabBar final : public design::DocumentTabBar {
  public:
    using design::DocumentTabBar::DocumentTabBar;

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
        addCorner_ = new design::TabAddCorner(this);
        addCorner_->addButton()->setObjectName("addSqlTabButton");
        addCorner_->addButton()->setAccessibleName(tr("New SQL query tab"));
        addCorner_->addButton()->setToolTip(tr("New SQL query tab"));
    }
    WorkspaceTabBar* workspaceBar() const { return static_cast<WorkspaceTabBar*>(tabBar()); }
    design::Button* addButton() const { return addCorner_->addButton(); }

  private:
    design::TabAddCorner* addCorner_;
};

} // namespace choscordb::main_window_detail
