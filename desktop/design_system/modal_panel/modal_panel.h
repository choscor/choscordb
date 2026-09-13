#pragma once
#include "design_system/dialog_presentation/dialog_presentation.h"
#include <QDialog>

namespace choscordb::design {

// Application modal surface; standard QDialog rejection never accepts an action.
class ModalPanel : public QDialog {
    Q_OBJECT
  public:
    explicit ModalPanel(QWidget* parent);
    ~ModalPanel() override;
    [[nodiscard]] QSize sizeHint() const override;

  public slots:
    void open() override;

  protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    DialogPresentation presentation_;
};
} // namespace choscordb::design
