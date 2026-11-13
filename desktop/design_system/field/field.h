#pragma once

#include <QPointer>
#include <QWidget>
class QFocusFrame;
class QWidget;
class QEvent;
class QLabel;

namespace choscordb::design {
class FieldValidation final : public QWidget {
  public:
    explicit FieldValidation(QWidget* control, QWidget* parent = nullptr);
    QWidget* control() const { return control_; }
    QString error() const;
    void setError(const QString& message);

  private:
    QWidget* control_;
    QLabel* errorLabel_;
};
}

namespace choscordb::design::detail {
void handleFieldFocusEvent(QWidget* widget, QEvent* event, QPointer<QFocusFrame>& focusFrame);
}
