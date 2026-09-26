#pragma once

#include "design_system/button/button.h"
#include "design_system/theme.h"
#include <QEvent>
#include <QPainter>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>

namespace choscordb::design {
class TabEndCap final : public QWidget {
  public:
    TabEndCap(QTabWidget* tabs, int width) : QWidget(tabs), tabs_(tabs), width_(width) {}

    QSize sizeHint() const override {
        return {width_, qMax(width_, tabs_->tabBar()->sizeHint().height())};
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), resolvedThemeForWidget(*this).colors.muted);
    }

  private:
    QTabWidget* tabs_;
    int width_;
};

// Keeps one add action next to the last tab, or fixed at the trailing edge during overflow.
class TabAddCorner final : public QWidget {
  public:
    explicit TabAddCorner(QTabWidget* tabs) : QWidget(tabs), tabs_(tabs) {
        setObjectName("tabAddCorner");
        addButton_ = new Button({}, this);
        addButton_->setAccessibleName(tr("Add tab"));
        addButton_->setToolTip(tr("Add tab"));
        addButton_->setDesignIcon(Icon::Add);
        addButton_->setButtonSize(ButtonSize::IconSmall);
        addButton_->setButtonContext(ButtonContext::TabAction);
        addButton_->setVariant(ButtonVariant::Ghost);
        addButton_->move(leftPadding_, 0);
        tabs_->setCornerWidget(new TabEndCap(tabs_, addButton_->width() + leftPadding_),
                               Qt::TopRightCorner);
        tabs_->installEventFilter(this);
        tabs_->tabBar()->installEventFilter(this);
        connect(tabs_->tabBar(), &QTabBar::currentChanged, this, [this] { schedulePosition(); });
        schedulePosition();
    }

    Button* addButton() const { return addButton_; }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if ((watched == tabs_ || watched == tabs_->tabBar()) &&
            (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest ||
             event->type() == QEvent::Show))
            schedulePosition();
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), resolvedThemeForWidget(*this).colors.muted);
    }

  private:
    void schedulePosition() {
        if (positionPending_)
            return;
        positionPending_ = true;
        QTimer::singleShot(0, this, [this] {
            positionPending_ = false;
            const auto* bar = tabs_->tabBar();
            const bool overflow = bar->sizeHint().width() > bar->width();
            const int tabEnd = tabs_->count() ? bar->tabRect(tabs_->count() - 1).right() + 1 : 0;
            const int x = overflow ? bar->width() : qMin(tabEnd, bar->width());
            const int y = bar->mapTo(tabs_, QPoint(0, 0)).y();
            const int height = tabs_->count() ? bar->tabRect(0).height() : addButton_->height();
            setGeometry(x, y, addButton_->width() + leftPadding_, height);
            raise();
        });
    }

    QTabWidget* tabs_;
    Button* addButton_;
    const int leftPadding_ = spacing(Spacing::Two);
    bool positionPending_ = false;
};
} // namespace choscordb::design
