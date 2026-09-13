#pragma once

#include "design_system/theme.h"

#include <QLabel>

namespace choscordb::design {

// Plain descriptive text. Selectable diagnostics use the existing text editors.
class Text final : public QLabel {
    Q_OBJECT
  public:
    explicit Text(const QString& text = {}, QWidget* parent = nullptr);
    void setTypographyRole(TypographyRole role);
    [[nodiscard]] TypographyRole typographyRole() const;
    void setWeight(QFont::Weight weight);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] int heightForWidth(int width) const override;

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    TypographyRole role_ = TypographyRole::Ui;
};

} // namespace choscordb::design
