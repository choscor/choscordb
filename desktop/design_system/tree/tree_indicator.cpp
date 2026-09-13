#include "design_system/tree/tree_indicator.h"
#include "design_system/control_glyphs/arrow_indicator.h"
#include "design_system/theme.h"
#include <QStyleOption>

namespace choscordb::design::detail {
bool drawTreeIndicator(QStyle::PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget) {
    if (element == QStyle::PE_IndicatorBranch) {
        if (option->state.testFlag(QStyle::State_Children)) {
            const auto arrow =
                option->state.testFlag(QStyle::State_Open)
                    ? QStyle::PE_IndicatorArrowDown
                    : (option->direction == Qt::RightToLeft ? QStyle::PE_IndicatorArrowLeft
                                                            : QStyle::PE_IndicatorArrowRight);
            drawArrowIndicator(arrow, option, painter, widget);
        }
        return true;
    }
    return false;
}

} // namespace choscordb::design::detail
