#include "design_system/field/field.h"
#include "design_system/theme.h"
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QFocusEvent>
#include <QFocusFrame>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace choscordb::design::detail {
class FieldFocusFrame final : public QFocusFrame {
  public:
    explicit FieldFocusFrame(QWidget* parent) : QFocusFrame(parent) {}

  protected:
    void paintEvent(QPaintEvent*) override {
        if (widget() == nullptr) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto palette = widget()->window()->palette();
        auto color = widget()->property("invalid").toBool() ? palette.color(QPalette::BrightText)
                                                            : palette.color(QPalette::Dark);
        QVariant themeValue;
        for (auto* ancestor = widget(); ancestor; ancestor = ancestor->parentWidget()) {
            if (ancestor->property("designTheme").isValid()) {
                themeValue = ancestor->property("designTheme");
                break;
            }
        }
        if (!themeValue.isValid()) {
            themeValue = qApp->property("designTheme");
        }
        if (themeValue.canConvert<ResolvedTheme>()) {
            const auto theme = themeValue.value<ResolvedTheme>();
            color = widget()->property("invalid").toBool() ? theme.colors.destructive
                                                           : theme.colors.focus;
        }
        painter.setPen(QPen(color, focusSpec().ringWidth));
        painter.setBrush(Qt::NoBrush);
        const auto rounding = radius(Radius::Large);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), rounding, rounding);
    }
};
bool isField(const QWidget* widget) {
    return qobject_cast<const QLineEdit*>(widget) || qobject_cast<const QComboBox*>(widget) ||
           qobject_cast<const QAbstractSpinBox*>(widget) ||
           qobject_cast<const QPlainTextEdit*>(widget) || qobject_cast<const QTextEdit*>(widget) ||
           qobject_cast<const QKeySequenceEdit*>(widget);
}
void handleFieldFocusEvent(QWidget* field, QEvent* event, QPointer<QFocusFrame>& focusFrame_) {
    if (field && isField(field) && event->type() == QEvent::FocusIn) {
        const auto reason = static_cast<QFocusEvent*>(event)->reason();
        if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
            reason == Qt::ShortcutFocusReason) {
            auto* target = isField(field->parentWidget()) ? field->parentWidget() : field;
            if (!focusFrame_) {
                focusFrame_ = new FieldFocusFrame(target);
            }
            focusFrame_->setWidget(target);
            focusFrame_->clearMask();
            focusFrame_->raise();
        }
    } else if (field && event->type() == QEvent::FocusOut && focusFrame_) {
        focusFrame_->setWidget(nullptr);
    }
}

} // namespace choscordb::design::detail
