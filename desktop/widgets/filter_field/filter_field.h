#pragma once

#include <QLineEdit>
#include <QTimer>

namespace choscordb {
namespace design {
class Button;
}

class FilterField final : public QLineEdit {
    Q_OBJECT

  public:
    explicit FilterField(QWidget* parent = nullptr);

  signals:
    void filterChanged(const QString& text);

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    void showClear(bool visible);
    void placeClear();
    QTimer delay_;
    design::Button* clear_;
};

}
