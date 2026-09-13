#pragma once

#include "design_system/modal_panel.h"

#include <QColor>
#include <QMessageBox>

class QLabel;
class QScrollArea;

namespace choscordb {
namespace design {
class Text;
}

// Retains QMessageBox's public button/role/result contract for workflow callers.
class ConfirmationDialog final : public QMessageBox {
    Q_OBJECT
  public:
    ConfirmationDialog(Icon icon, const QString& title, const QString& text,
                       StandardButtons buttons = NoButton, QWidget* parent = nullptr);
    static StandardButton question(QWidget* parent, const QString& title, const QString& text,
                                   StandardButtons buttons = StandardButtons(Yes | No),
                                   StandardButton defaultButton = NoButton);

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

} // namespace choscordb
