#include "design_system/tabs/segmented_control.h"
#include "design_system/button/button.h"
#include "design_system/metrics/metrics.h"
#include "design_system/theme.h"
#include <QHBoxLayout>
#include <QPainter>

namespace choscordb::design {
SegmentedControl::SegmentedControl(QWidget* parent)
    : QWidget(parent), layout_(new QHBoxLayout(this)) {
    const int inset = spacing(Spacing::Half);
    layout_->setContentsMargins(inset, inset, inset, inset);
    layout_->setSpacing(0);
    setFixedHeight(dimension(Dimension::Tab));
}
void SegmentedControl::addSegment(Button* segment) {
    segment->setParent(this);
    segment->setButtonContext(ButtonContext::Segment);
    segment->setVariant(ButtonVariant::Ghost);
    segment->setButtonSize(ButtonSize::Icon);
    segment->setCheckable(true);
    segment->setMinimumWidth(0);
    segment->setMaximumWidth(QWIDGETSIZE_MAX);
    segment->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout_->addWidget(segment, 1);
}
void SegmentedControl::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(resolvedThemeForWidget(*this).colors.recessed);
    const int rounding = radius(Radius::Large);
    painter.drawRoundedRect(QRectF(rect()), rounding, rounding);
}
}
