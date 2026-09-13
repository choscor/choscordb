#pragma once

#include "design_system/icons.h"
#include <QPushButton>
#include <QStyle>
#include <optional>

namespace choscordb::design {
enum class ButtonContext { Standard, EditorAction };
enum class ButtonVariant { Default, Secondary, Outline, Ghost, Destructive, Link };
enum class ButtonSize {
    ExtraSmall,
    Small,
    Default,
    Large,
    IconExtraSmall,
    IconSmall,
    Icon,
    IconLarge
};

// Keeps Qt's action, shortcut, accessibility and toggle semantics.
class Button : public QPushButton {
    Q_OBJECT
  public:
    explicit Button(const QString& text, QWidget* parent = nullptr);
    void setDesignIcon(Icon icon);
    void setIcon(const QIcon& icon);
    void setButtonContext(ButtonContext context);
    [[nodiscard]] ButtonContext buttonContext() const;
    void setVariant(ButtonVariant variant);
    [[nodiscard]] ButtonVariant variant() const;
    void setButtonSize(ButtonSize size);
    [[nodiscard]] ButtonSize buttonSize() const;
    void setLoading(bool loading);
    [[nodiscard]] bool isLoading() const;
    [[nodiscard]] QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    [[nodiscard]] virtual QStyle::State visualState() const;

  private:
    std::optional<Icon> designIcon_;
    QIcon paintedIcon_;
    QColor paintedIconColor_;
    ButtonContext context_ = ButtonContext::Standard;
    ButtonVariant variant_ = ButtonVariant::Default;
    ButtonSize size_ = ButtonSize::Default;
    bool keyboardFocus_ = false;
    bool loading_ = false;
    bool enabledBeforeLoading_ = true;
};
} // namespace choscordb::design
