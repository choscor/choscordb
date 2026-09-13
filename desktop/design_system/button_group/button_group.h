#pragma once

#include <QWidget>

class QBoxLayout;

namespace choscordb::design {
class Button;

class ButtonGroup final : public QWidget {
    Q_OBJECT
  public:
    explicit ButtonGroup(Qt::Orientation orientation = Qt::Horizontal, QWidget* parent = nullptr);
    void addButton(Button* button);

  private:
    QBoxLayout* layout_;
};
} // namespace choscordb::design
