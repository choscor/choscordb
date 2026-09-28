#include "design_system/status_line/status_line.h"

#include "design_system/fonts/fonts.h"
#include "design_system/metrics/metrics.h"
#include "design_system/theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPalette>

namespace choscordb::design {

StatusLine::StatusLine(QWidget* parent) : QWidget(parent) {
    const auto metrics = resolveMetrics(Density::Compact, true);
    setFont(resolveTypography(TypographyRole::Ui));
    content_ = new QHBoxLayout(this);
    content_->setContentsMargins(metrics.spacingMedium, metrics.spacingSmall, metrics.spacingMedium,
                                 metrics.spacingSmall);
    content_->setSpacing(metrics.spacingMedium);
    setAutoFillBackground(true);
    refreshAppearance();
}

QHBoxLayout* StatusLine::contentLayout() const {
    return content_;
}

void StatusLine::setAvailable(bool available) {
    if (available_ == available)
        return;
    available_ = available;
    refreshAppearance();
}

void StatusLine::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange ||
        event->type() == QEvent::ParentChange)
        refreshAppearance();
}

void StatusLine::paintEvent(QPaintEvent*) {
    const auto colors = resolvedThemeForWidget(*this).colors;
    QPainter painter(this);
    painter.fillRect(rect(), available_ ? colors.successSurface : colors.dangerSurface);
}

void StatusLine::refreshAppearance() {
    if (refreshing_)
        return;
    refreshing_ = true;
    const auto colors = resolvedThemeForWidget(*this).colors;
    auto colorsForLine = palette();
    colorsForLine.setColor(QPalette::Window,
                           available_ ? colors.successSurface : colors.dangerSurface);
    colorsForLine.setColor(QPalette::WindowText, available_ ? colors.success : colors.danger);
    setPalette(colorsForLine);
    refreshing_ = false;
}

} // namespace choscordb::design
