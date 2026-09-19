#include "design_system/spin_box/spin_box_style.h"

namespace choscordb::design {
QString spinBoxStyleSheet() {
    return QStringLiteral(R"(QSpinBox, QDoubleSpinBox { padding-right: 24px; font-weight: 400; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: padding; subcontrol-position: center right; position: relative; top: -7px; width: 20px; height: 14px; border: 0; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: padding; subcontrol-position: center right; position: relative; top: 7px; width: 20px; height: 14px; border: 0; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow, QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { width: 12px; height: 12px; }
)");
}
} // namespace choscordb::design
