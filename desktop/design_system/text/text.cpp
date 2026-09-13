#include "design_system/text/text.h"

#include <QPainter>
#include <QStyle>
#include <QTextLayout>
#include <cmath>
#include <limits>

namespace choscordb::design {
namespace {

QSize measure(const QString& text, const QFont& font, int lineHeight, int width, bool wrap,
              Qt::Alignment alignment = Qt::AlignLeft, QPainter* painter = nullptr,
              const QPointF& offset = {}) {
    qreal measuredWidth = 0;
    int height = 0;
    for (const auto& paragraph : text.split(QLatin1Char('\n'))) {
        QTextLayout layout(paragraph, font);
        QTextOption option;
        option.setAlignment(alignment);
        option.setWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
        layout.setTextOption(option);
        layout.beginLayout();
        int count = 0;
        while (true) {
            auto line = layout.createLine();
            if (!line.isValid()) {
                break;
            }
            line.setLineWidth(qMax(1, width));
            measuredWidth = qMax(measuredWidth, line.naturalTextWidth());
            line.setPosition(QPointF(0, height + (lineHeight - line.height()) / 2.0));
            height += lineHeight;
            ++count;
        }
        layout.endLayout();
        if (count == 0) {
            height += lineHeight;
        }
        if (painter != nullptr) {
            layout.draw(painter, offset);
        }
    }
    return {static_cast<int>(std::ceil(measuredWidth)), height};
}

} // namespace

Text::Text(const QString& text, QWidget* parent) : QLabel(text, parent) {
    setTextFormat(Qt::PlainText);
    setTextInteractionFlags(Qt::NoTextInteraction);
    setTypographyRole(TypographyRole::Ui);
}
void Text::setTypographyRole(TypographyRole role) {
    role_ = role;
    setFont(resolveTypography(role));
    updateGeometry();
    update();
}
TypographyRole Text::typographyRole() const {
    return role_;
}
void Text::setWeight(QFont::Weight weight) {
    auto updated = font();
    updated.setWeight(weight);
    setFont(updated);
}
QSize Text::sizeHint() const {
    const auto padding = 2 * (margin() + frameWidth());
    const auto measured = measure(text(), font(), typographySpec(role_).lineHeight,
                                  std::numeric_limits<int>::max() / 4, false);
    return measured + QSize(padding, padding);
}
QSize Text::minimumSizeHint() const {
    if (!wordWrap()) {
        return sizeHint();
    }
    const auto padding = 2 * (margin() + frameWidth());
    const auto minimum = measure(text(), font(), typographySpec(role_).lineHeight, 1, true);
    return {padding + minimum.width(), padding + typographySpec(role_).lineHeight};
}
int Text::heightForWidth(int width) const {
    const auto padding = 2 * (margin() + frameWidth());
    return measure(text(), font(), typographySpec(role_).lineHeight, width - padding, wordWrap())
               .height() +
           padding;
}
void Text::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    drawFrame(&painter);
    const auto area = contentsRect().adjusted(margin(), margin(), -margin(), -margin());
    painter.setClipRect(area);
    painter.setPen(palette().color(isEnabled() ? palette().currentColorGroup() : QPalette::Disabled,
                                   foregroundRole()));
    const auto lineHeight = typographySpec(role_).lineHeight;
    const auto contentHeight =
        measure(text(), font(), lineHeight, area.width(), wordWrap()).height();
    qreal y = area.y();
    if (alignment().testFlag(Qt::AlignVCenter)) {
        y += (area.height() - contentHeight) / 2.0;
    } else if (alignment().testFlag(Qt::AlignBottom)) {
        y += area.height() - contentHeight;
    }
    measure(text(), font(), lineHeight, area.width(), wordWrap(),
            QStyle::visualAlignment(layoutDirection(), alignment()) & Qt::AlignHorizontal_Mask,
            &painter, QPointF(area.x(), y));
}

} // namespace choscordb::design
