#include "design_system/control_style.h"
#include "design_system/checkbox/checkbox_indicator.h"
#include "design_system/control_glyphs/arrow_indicator.h"
#include "design_system/control_glyphs/control_glyphs.h"
#include "design_system/field/field.h"
#include "design_system/field/focus_indicator.h"
#include "design_system/menu/menu.h"
#include "design_system/menu/menu_indicator.h"
#include "design_system/select/select_popup.h"
#include "design_system/tabs/tab_indicator.h"
#include "design_system/tooltip/tooltip.h"
#include "design_system/tree/tree_indicator.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDynamicPropertyChangeEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QStyleFactory>
#include <QTabBar>
#include <QToolButton>

namespace choscordb::design {
void ControlStyle::polish(QWidget* widget) {
    QProxyStyle::polish(widget);
    if (auto* tabs = qobject_cast<QTabBar*>(widget))
        tabs->setDrawBase(false);
    if (qobject_cast<QLineEdit*>(widget) || qobject_cast<QComboBox*>(widget) ||
        qobject_cast<QAbstractSpinBox*>(widget) || qobject_cast<QKeySequenceEdit*>(widget)) {
        widget->setFont(resolveTypography(TypographyRole::Field));
        if (auto* line = qobject_cast<QLineEdit*>(widget)) {
            const auto updateFont = [line] {
                auto font = resolveTypography(TypographyRole::Field);
                if (line->text().isEmpty() && !line->placeholderText().isEmpty())
                    font.setWeight(QFont::Normal);
                line->setFont(font);
            };
            if (!line->property("fieldFontConnected").toBool()) {
                line->setProperty("fieldFontConnected", true);
                connect(line, &QLineEdit::textChanged, line, updateFont);
            }
            updateFont();
        }
    } else if (qobject_cast<QPushButton*>(widget) || qobject_cast<QToolButton*>(widget)) {
        auto font = widget->font();
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
        widget->setFont(font);
    }

    detail::polishMenu(widget);
    detail::polishControlGlyphs(widget);
    widget->installEventFilter(this);
}
void ControlStyle::unpolish(QWidget* widget) {
    widget->removeEventFilter(this);
    detail::unpolishControlGlyphs(widget);
    if (focusFrame_ && focusFrame_->widget() == widget) {
        focusFrame_->setWidget(nullptr);
    }
    QProxyStyle::unpolish(widget);
}
bool ControlStyle::eventFilter(QObject* watched, QEvent* event) {
    auto* field = qobject_cast<QWidget*>(watched);
    detail::prepareStandardContextMenu(field, event);
    if ((qobject_cast<QAbstractSpinBox*>(field) || qobject_cast<QComboBox*>(field)) &&
        event->type() == QEvent::Wheel) {
        event->ignore();
        return true;
    }
    if (auto* combo = qobject_cast<QComboBox*>(field);
        combo && detail::handleFontComboResize(*combo, event))
        return true;
    detail::positionSubmenu(field, event);
    if (auto* combo = qobject_cast<QComboBox*>(field);
        combo && (event->type() == QEvent::Show || event->type() == QEvent::PaletteChange)) {
        detail::prepareComboPopup(*combo);
    }
    detail::updateControlGlyphs(field, event);
    if (field && event->type() == QEvent::DynamicPropertyChange) {
        const auto name = static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName();
        if (name == "invalid" || name == "state" || name == "variant" || name == "designRole" ||
            name == "primary" || name == "iconOnly") {
            const bool restoreFocus = focusFrame_ && focusFrame_->widget() == field;
            field->style()->unpolish(field);
            field->style()->polish(field);
            if (restoreFocus && focusFrame_) {
                focusFrame_->setWidget(field);
                focusFrame_->clearMask();
                focusFrame_->raise();
            }
            field->update();
        }
    }
    if (detail::handleTooltipEvent(field, event, tooltip_, tooltipOwner_)) {
        return true;
    }
    detail::handleFieldFocusEvent(field, event, focusFrame_);
    return QProxyStyle::eventFilter(watched, event);
}
ControlStyle::ControlStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
int ControlStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                            QStyleHintReturn* returnData) const {
    // A list popup uses the view's item styling. The menu delegate instead
    // paints each option through the combo's field style, including its frame.
    if (hint == SH_ComboBox_Popup)
        return 0;
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}
void ControlStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                                 QPainter* painter, const QWidget* widget) const {
    if (detail::drawMenuIndicator(element, option, painter, widget)) {
        return;
    }
    if (detail::drawTabIndicator(element, option, painter, widget)) {
        return;
    }
    if (detail::drawTreeIndicator(element, option, painter, widget)) {
        return;
    }
    if (detail::drawFocusIndicator(element, option, painter, widget)) {
        return;
    }
    if (detail::drawArrowIndicator(element, option, painter, widget)) {
        return;
    }
    if (detail::drawCheckboxIndicator(element, option, painter, widget)) {
        return;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}
int ControlStyle::pixelMetric(PixelMetric metric, const QStyleOption* option,
                              const QWidget* widget) const {
    if (metric == PM_ExclusiveIndicatorWidth || metric == PM_ExclusiveIndicatorHeight)
        return 18;
    // Anchor nested menus to the painted panel, excluding transparent shadow
    // padding from their separation. Qt handles the mirrored/edge placement.
    if (metric == PM_SubMenuOverlap && qobject_cast<const QMenu*>(widget))
        return spacing(Spacing::One) - detail::menuShadowMargin();
    if (metric == PM_HeaderDefaultSectionSizeVertical)
        return dimension(Dimension::TableRow);
    if (metric == PM_FocusFrameHMargin || metric == PM_FocusFrameVMargin) {
        return 3;
    }
    if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight) {
        if (widget && widget->property("designRole").toString() == QLatin1String("switch"))
            return dimension(metric == PM_IndicatorWidth ? Dimension::SwitchWidth
                                                         : Dimension::SwitchHeight);
        return dimension(Dimension::Checkbox);
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}
} // namespace choscordb::design
