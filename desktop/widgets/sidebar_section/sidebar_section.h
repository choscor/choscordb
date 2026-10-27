#pragma once

#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace choscordb {
class SidebarSection final : public QWidget {
  public:
    explicit SidebarSection(const QString& title, QWidget* parent = nullptr) : QWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(design::spacing(design::Spacing::One));
        auto* header = new QHBoxLayout;
        header->setSpacing(design::spacing(design::Spacing::One));
        title_ = new design::Text(title, this);
        title_->setSectionLabel();
        header->addWidget(title_);
        busy_ = new QLabel(this);
        busy_->setObjectName("sidebarSectionBusy");
        busy_->hide();
        header->addWidget(busy_);
        header->addStretch();
        actions_ = new QHBoxLayout;
        actions_->setSpacing(design::spacing(design::Spacing::One));
        header->addLayout(actions_);
        layout->addLayout(header);
        content_ = new QVBoxLayout;
        content_->setSpacing(design::spacing(design::Spacing::One));
        layout->addLayout(content_, 1);
    }
    design::Text* titleLabel() const { return title_; }
    void addAction(QWidget* action) { actions_->addWidget(action); }
    QVBoxLayout* contentLayout() const { return content_; }
    // Sidebar progress shows only as a Loader icon after the title; the description is for
    // the tooltip and accessibility.
    void setBusy(bool busy, const QString& description = {}) {
        if (busy == !busy_->isHidden() && description == busy_->toolTip())
            return;
        busy_->setToolTip(description);
        busy_->setAccessibleName(description);
        if (busy)
            refreshBusyIcon();
        busy_->setVisible(busy);
    }

  protected:
    void changeEvent(QEvent* event) override {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
            refreshBusyIcon();
    }

  private:
    void refreshBusyIcon() {
        const int size = design::dimension(design::Dimension::IconSmall);
        busy_->setPixmap(design::themedIcon(design::Icon::Loader,
                                            design::resolvedThemeForWidget(*this).colors.fgMuted,
                                            size)
                             .pixmap(size, size));
    }
    QLabel* busy_;
    design::Text* title_;
    QHBoxLayout* actions_;
    QVBoxLayout* content_;
};
} // namespace choscordb
