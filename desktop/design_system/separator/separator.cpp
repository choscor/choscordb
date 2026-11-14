#include "design_system/separator/separator.h"
#include "design_system/metrics/metrics.h"
#include <QFrame>

namespace choscordb::design {
QFrame* createToolbarSeparator(QWidget* parent) {
    auto* separator = new QFrame(parent);
    separator->setObjectName("toolbarSeparator");
    separator->setFrameShape(QFrame::VLine);
    separator->setFixedHeight(dimension(Dimension::ControlSmall));
    return separator;
}
}
