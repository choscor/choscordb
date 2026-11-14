#pragma once

#include "design_system/dialog_presentation/dialog_presentation.h"

#include <QColor>
#include <QMessageBox>

class QLabel;
class QPushButton;
class QScrollArea;

namespace choscordb {
namespace design {
class Text;
}

class ConfirmationDialog final : public QMessageBox {
    Q_OBJECT
  public:
    ConfirmationDialog(Icon icon, const QString& title, const QString& text,
                       StandardButtons buttons = NoButton, QWidget* parent = nullptr);
    using QMessageBox::open;
    enum class Consequence { Safe, Destructive };
    static bool confirm(QWidget* parent, const QString& title, const QString& text,
                        const QString& action, Consequence consequence);
    QPushButton* addActionButton(const QString& text, Consequence consequence);
    template <typename... Arguments> static void question(Arguments&&...) = delete;

  public slots:
    void open() override;

  protected:
    bool event(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    void prepareContent();
    void refreshIcon();
    QLabel* heading_ = nullptr;
    QScrollArea* bodyScroll_ = nullptr;
    design::Text* bodyText_ = nullptr;
    Icon sourceIcon_ = NoIcon;
    QColor iconTint_;
    design::DialogPresentation presentation_;
};

}
