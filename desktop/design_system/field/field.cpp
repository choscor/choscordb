#include "design_system/field/field.h"
#include "design_system/theme.h"
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QFocusEvent>
#include <QFocusFrame>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QStyle>
#include <QTextEdit>
#include <QVBoxLayout>

namespace choscordb::design {
FieldValidation::FieldValidation(QWidget* control, QWidget* parent)
    : QWidget(parent), control_(control), errorLabel_(new QLabel(this)) {
    Q_ASSERT(control_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    control_->setParent(this);
    setFocusProxy(control_);
    setProperty("originalAccessibleDescription", control_->accessibleDescription());
    layout->addWidget(control_);
    errorLabel_->setProperty("state", "error");
    errorLabel_->setProperty("designRole", "fieldError");
    errorLabel_->setWordWrap(true);
    errorLabel_->hide();
    layout->addWidget(errorLabel_);
}

QString FieldValidation::error() const {
    return errorLabel_->text();
}

void FieldValidation::setError(const QString& message) {
    errorLabel_->setText(message);
    errorLabel_->setVisible(!message.isEmpty());
    control_->setProperty("invalid", !message.isEmpty());
    const auto original = property("originalAccessibleDescription").toString();
    control_->setAccessibleDescription(message.isEmpty()    ? original
                                       : original.isEmpty() ? message
                                                            : original + ". " + message);
    control_->style()->unpolish(control_);
    control_->style()->polish(control_);
    control_->update();
}
} // namespace choscordb::design

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
