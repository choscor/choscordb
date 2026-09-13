#include "design_system/menu/menu_indicator.h"
#include "design_system/icons.h"
#include "design_system/theme.h"
#include <QStyleOption>

namespace choscordb::design::detail {
bool drawMenuIndicator(QStyle::PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget*) {
    if (element == QStyle::PE_IndicatorMenuCheckMark) {
        const auto color = option->palette.color(option->state.testFlag(QStyle::State_Selected)
                                                     ? QPalette::HighlightedText
                                                     : QPalette::ButtonText);
        themedIcon(Icon::Check, color, 16).paint(painter, option->rect);
        return true;
    }
    return false;
}

} // namespace choscordb::design::detail
