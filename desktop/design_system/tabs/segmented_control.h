#pragma once

#include <QWidget>

class QHBoxLayout;

namespace choscordb::design {
class Button;

class SegmentedControl final : public QWidget {
    Q_OBJECT
  public:
    explicit SegmentedControl(QWidget* parent = nullptr);
    void addSegment(Button* segment);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QHBoxLayout* layout_;
};
}
