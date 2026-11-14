#include "widgets/filter_field/filter_field.h"

#include "bridge/engine_adapter.h"
#include "design_system/button/button.h"
#include "design_system/metrics/metrics.h"

namespace choscordb {

FilterField::FilterField(QWidget* parent)
    : QLineEdit(parent), clear_(design::createDismissButton(tr("Clear filter"), this)) {
    delay_.setSingleShot(true);
    delay_.setInterval(EngineAdapter::filterDelayMs());
    clear_->setObjectName("filterFieldClear");
    clear_->setFocusPolicy(Qt::NoFocus);
    clear_->setCursor(Qt::ArrowCursor);
    showClear(false);
    connect(clear_, &QPushButton::clicked, this, &QLineEdit::clear);
    connect(&delay_, &QTimer::timeout, this, [this] { emit filterChanged(text()); });
    connect(this, &QLineEdit::textChanged, this, [this](const QString& value) {
        showClear(!value.isEmpty());
        if (value.isEmpty()) {
            delay_.stop();
            emit filterChanged(value);
            return;
        }
        delay_.start();
    });
}

void FilterField::showClear(bool visible) {
    clear_->setVisible(visible);
    setTextMargins(0, 0, visible ? clear_->width() : 0, 0);
    placeClear();
}

void FilterField::placeClear() {
    clear_->move(width() - clear_->width() - design::spacing(design::Spacing::Two),
                 (height() - clear_->height()) / 2);
}

void FilterField::resizeEvent(QResizeEvent* event) {
    QLineEdit::resizeEvent(event);
    placeClear();
}

}
