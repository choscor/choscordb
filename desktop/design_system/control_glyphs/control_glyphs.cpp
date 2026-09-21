#include "design_system/control_glyphs/control_glyphs.h"
#include "design_system/style/scoped_theme.h"
#include "design_system/theme.h"
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSpinBox>
#include <QStyleOption>

namespace choscordb::design::detail {
// QStyleSheetStyle paints styled arrow rules itself, bypassing proxy primitives.
// This shared child paints only those glyphs; Qt keeps all hit testing and input.
class ControlGlyphOverlay final : public QWidget {
  public:
    explicit ControlGlyphOverlay(QWidget* owner) : QWidget(owner) {
        setObjectName("designControlGlyphs");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(owner->rect());
        if (auto* spin = qobject_cast<QSpinBox*>(owner)) {
            connect(spin, &QSpinBox::valueChanged, this, [this] { update(); });
        } else if (auto* doubleSpin = qobject_cast<QDoubleSpinBox*>(owner)) {
            connect(doubleSpin, &QDoubleSpinBox::valueChanged, this, [this] { update(); });
        } else if (auto* combo = qobject_cast<QComboBox*>(owner)) {
            combo->view()->parentWidget()->installEventFilter(this);
        }
        show();
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide)
            update();
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override {
        auto* owner = parentWidget();
        if (!owner) {
            return;
        }
        auto color = owner->palette().color(QPalette::ButtonText);
        const auto themeValue = scopedThemeValue(owner);
        if (themeValue.canConvert<ResolvedTheme>()) {
            color = themeValue.value<ResolvedTheme>().colors.foreground;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto drawArrow = [&](const QRect& area, bool up, bool enabled, qreal size) {
            if (area.isEmpty()) {
                return;
            }
            painter.save();
            painter.setOpacity(enabled ? 1.0 : 0.5);
            painter.translate(QRectF(area).center());
            if (up) {
                painter.rotate(180);
            }
            painter.scale(size / 16.0, size / 16.0);
            painter.setPen(QPen(color, 4.0 / 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath arrow;
            arrow.moveTo(-4, -2);
            arrow.lineTo(0, 2);
            arrow.lineTo(4, -2);
            painter.drawPath(arrow);
            painter.restore();
        };
        if (auto* combo = qobject_cast<QComboBox*>(owner)) {
            QStyleOptionComboBox option;
            option.initFrom(combo);
            option.editable = combo->isEditable();
            option.frame = combo->hasFrame();
            auto arrow = combo->style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                        QStyle::SC_ComboBoxArrow, combo);
            // Match the reference select's trailing glyph alignment. Qt's
            // native subcontrol retains the original, larger hit region.
            arrow.translate(combo->layoutDirection() == Qt::RightToLeft ? -2 : 2, 0);
            drawArrow(arrow, combo->view()->isVisible(), combo->isEnabled(), 16);
        } else if (auto* spin = qobject_cast<QAbstractSpinBox*>(owner)) {
            if (spin->buttonSymbols() == QAbstractSpinBox::NoButtons) {
                return;
            }
            QStyleOptionSpinBox option;
            option.initFrom(spin);
            option.frame = spin->hasFrame();
            option.buttonSymbols = spin->buttonSymbols();
            bool up = spin->isEnabled() && !spin->isReadOnly();
            bool down = up;
            if (!spin->wrapping()) {
                if (auto* integer = qobject_cast<QSpinBox*>(spin)) {
                    up = up && integer->value() < integer->maximum();
                    down = down && integer->value() > integer->minimum();
                } else if (auto* real = qobject_cast<QDoubleSpinBox*>(spin)) {
                    up = up && real->value() < real->maximum();
                    down = down && real->value() > real->minimum();
                }
            }
            drawArrow(spin->style()->subControlRect(QStyle::CC_SpinBox, &option,
                                                    QStyle::SC_SpinBoxUp, spin),
                      true, up, 12);
            drawArrow(spin->style()->subControlRect(QStyle::CC_SpinBox, &option,
                                                    QStyle::SC_SpinBoxDown, spin),
                      false, down, 12);
        }
    }
};
void polishControlGlyphs(QWidget* widget) {
    if ((qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractSpinBox*>(widget)) &&
        !widget->findChild<QWidget*>("designControlGlyphs", Qt::FindDirectChildrenOnly)) {
        new ControlGlyphOverlay(widget);
    }
}
void unpolishControlGlyphs(QWidget* widget) {
    delete widget->findChild<QWidget*>("designControlGlyphs", Qt::FindDirectChildrenOnly);
}
void updateControlGlyphs(QWidget* field, QEvent* event) {
    if (field &&
        (event->type() == QEvent::Resize || event->type() == QEvent::StyleChange ||
         event->type() == QEvent::PaletteChange || event->type() == QEvent::EnabledChange)) {
        if (auto* glyphs =
                field->findChild<QWidget*>("designControlGlyphs", Qt::FindDirectChildrenOnly)) {
            glyphs->setGeometry(field->rect());
            glyphs->raise();
            glyphs->update();
        }
    }
}

} // namespace choscordb::design::detail
