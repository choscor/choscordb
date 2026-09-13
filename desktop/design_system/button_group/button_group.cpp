#include "design_system/button_group/button_group.h"
#include "design_system/button/button.h"
#include <QBoxLayout>

namespace choscordb::design {
ButtonGroup::ButtonGroup(Qt::Orientation orientation, QWidget* parent)
    : QWidget(parent),
      layout_(new QBoxLayout(orientation == Qt::Horizontal ? QBoxLayout::LeftToRight
                                                           : QBoxLayout::TopToBottom,
                             this)) {
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
}
void ButtonGroup::addButton(Button* button) {
    if (!button)
        return;
    layout_->addWidget(button);
    for (int i = 0; i < layout_->count(); ++i) {
        auto* item = layout_->itemAt(i)->widget();
        item->setProperty("groupFirst", i == 0);
        item->setProperty("groupLast", i == layout_->count() - 1);
        item->setProperty("groupVertical", layout_->direction() == QBoxLayout::TopToBottom);
        item->update();
    }
}
} // namespace choscordb::design
