#pragma once
#include "design_system/dialog_presentation/dialog_presentation.h"
#include <QDialog>

namespace choscordb::design {
class DialogSections;

class ModalDialog : public QDialog {
    Q_OBJECT
  public:
    explicit ModalDialog(QWidget* parent);
    ~ModalDialog() override;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] DialogSections* createSections();

  public slots:
    void open() override;

  protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    DialogPresentation presentation_;
};
using ModalPanel = ModalDialog;
}
