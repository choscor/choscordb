#pragma once

#include <QString>
#include <QTabBar>

namespace choscordb::design {
class DocumentTabBar : public QTabBar {
  public:
    explicit DocumentTabBar(QWidget* parent = nullptr);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

  private:
    bool keyboardFocus_ = false;
};
}
