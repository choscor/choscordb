#pragma once

#include "design_system/theme.h"

#include <QFocusFrame>
#include <QPointer>
#include <QProxyStyle>

namespace choscordb::design {

// Palette-driven so independent preview roots can share the production style.
class ControlStyle final : public QProxyStyle {
  public:
    ControlStyle();
    void polish(QWidget* widget) override;
    void unpolish(QWidget* widget) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override;
    [[nodiscard]] int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                                  const QWidget* widget = nullptr) const override;

  private:
    QPointer<QFocusFrame> focusFrame_;
    QPointer<QWidget> tooltip_;
    QPointer<QWidget> tooltipOwner_;
};

[[nodiscard]] QString controlStyleSheet(const ResolvedTheme& theme);

} // namespace choscordb::design
