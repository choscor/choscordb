#pragma once
#include "design_system/dialog_presentation/dialog_presentation.h"
#include <QDialog>

namespace choscordb::design {

// Owner-window modal dialog. DialogPresentation owns its separate dimmed backdrop.
class ModalDialog : public QDialog {
    Q_OBJECT
  public:
    explicit ModalDialog(QWidget* parent);
    ~ModalDialog() override;
    [[nodiscard]] QSize sizeHint() const override;
    void setEdgeToEdgeContent(bool enabled);

  public slots:
    void open() override;

  protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    DialogPresentation presentation_;
    bool edgeToEdgeContent_ = false;
};
using ModalPanel = ModalDialog;
} // namespace choscordb::design
