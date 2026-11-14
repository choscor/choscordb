#pragma once

#include <QColor>
#include <QWidget>

class QVariantAnimation;

namespace choscordb::design {

class Spinner final : public QWidget {
    Q_OBJECT

  public:
    explicit Spinner(QWidget* parent = nullptr);
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] qreal angle() const;
    [[nodiscard]] bool isTurning() const;

  protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;

  private:
    void restart();
    QVariantAnimation* turn_;
    QVariantAnimation* dash_;
    QColor color_;
};

}
