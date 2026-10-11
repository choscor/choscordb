#pragma once

#include "design_system/theme.h"

#include <QLabel>

namespace choscordb::design {

// Plain descriptive text. Selectable diagnostics use the existing text editors.
class Text final : public QLabel {
    Q_OBJECT
  public:
    explicit Text(const QString& text = {}, QWidget* parent = nullptr);
    void setTextStyle(TextStyle style, FontDesign design = FontDesign::Default);
    void setWeight(QFont::Weight weight);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] int heightForWidth(int width) const override;

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    TextStyle style_ = TextStyle::Body;
    FontDesign design_ = FontDesign::Default;
};

} // namespace choscordb::design
