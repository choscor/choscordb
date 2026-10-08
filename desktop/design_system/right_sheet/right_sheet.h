#pragma once

#include "design_system/dialog_presentation/dialog_presentation.h"
#include <QDialog>

class QHBoxLayout;
class QScrollArea;

namespace choscordb::design {
class Text;

// Owner-contained modal sheet. Callers supply the body widget and footer actions.
class RightSheet final : public QDialog {
    Q_OBJECT
  public:
    explicit RightSheet(QWidget* owner);
    ~RightSheet() override;
    void setTitle(const QString& title);
    void setBody(QWidget* body);
    [[nodiscard]] QHBoxLayout* footerLayout() const;

  public slots:
    void open() override;

  protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    DialogPresentation presentation_;
    Text* title_ = nullptr;
    QScrollArea* body_ = nullptr;
    QHBoxLayout* footer_ = nullptr;
};
} // namespace choscordb::design
