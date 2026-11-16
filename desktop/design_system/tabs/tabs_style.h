#pragma once

#include "design_system/colors/colors.h"
#include <QString>
#include <QTabBar>
#include <optional>

namespace choscordb::design {
class DocumentTabBar : public QTabBar {
  public:
    explicit DocumentTabBar(QWidget* parent = nullptr);
    void setTabTag(int index, std::optional<StatusTone> tone, const QString& label);
    [[nodiscard]] std::optional<StatusTone> tabTone(int index) const;
    [[nodiscard]] QString tabTagLabel(int index) const;

  protected:
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

  private:
    bool keyboardFocus_ = false;
};
}
