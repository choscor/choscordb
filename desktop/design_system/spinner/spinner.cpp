#include "design_system/spinner/spinner.h"
#include "design_system/metrics/metrics.h"
#include "design_system/theme.h"
#include <QEvent>
#include <QPainter>
#include <QVariantAnimation>

namespace choscordb::design {

namespace {
struct LoaderGeometry final {
    qreal viewBox = 24;
    qreal radius = 9.5;
    qreal stroke = 2;
    qreal trackOpacity = 0.1;
    qreal growLength = 42;
    qreal growOffset = 16;
    qreal endOffset = 59;
};
constexpr LoaderGeometry loader;
constexpr int TurnDegrees = 360;

QVariantAnimation* loopingAnimation(QObject* parent, qreal end) {
    auto* animation = new QVariantAnimation(parent);
    animation->setStartValue(0.0);
    animation->setEndValue(end);
    animation->setLoopCount(-1);
    return animation;
}
}

Spinner::Spinner(QWidget* parent)
    : QWidget(parent), turn_(loopingAnimation(this, TurnDegrees)),
      dash_(loopingAnimation(this, 1.0)) {
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(sizeHint());
    connect(turn_, &QVariantAnimation::valueChanged, this, [this] { update(); });
}

QSize Spinner::sizeHint() const {
    const int size = dimension(Dimension::Icon);
    return {size, size};
}

qreal Spinner::angle() const {
    return turn_->state() == QAbstractAnimation::Running ? turn_->currentValue().toReal() : 0.0;
}

bool Spinner::isTurning() const {
    return turn_->state() == QAbstractAnimation::Running;
}

void Spinner::restart() {
    const auto theme = resolvedThemeForWidget(*this);
    color_ = theme.colors.textSubtle;
    turn_->stop();
    dash_->stop();
    const auto turn = motionSpec(Motion::LoaderTurn, theme.reducedMotion).durationMs;
    if (isVisible() && turn > 0) {
        turn_->setDuration(turn);
        dash_->setDuration(motionSpec(Motion::LoaderDash, theme.reducedMotion).durationMs);
        turn_->start();
        dash_->start();
    }
    update();
}

void Spinner::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal scale = width() / loader.viewBox;
    const qreal radius = loader.radius * scale;
    const QPointF center = QRectF(rect()).center();
    const QRectF circle(center.x() - radius, center.y() - radius, 2 * radius, 2 * radius);
    QPen pen(color_, loader.stroke * scale, Qt::SolidLine, Qt::RoundCap);
    painter.setOpacity(loader.trackOpacity);
    painter.setPen(pen);
    painter.drawEllipse(circle);
    painter.setOpacity(1.0);
    const qreal progress =
        dash_->state() == QAbstractAnimation::Running ? dash_->currentValue().toReal() : 0.5;
    const qreal grow = qMin(1.0, progress * 2);
    const qreal length = loader.growLength * grow;
    const qreal offset =
        progress < 0.5
            ? loader.growOffset * grow
            : loader.growOffset + (loader.endOffset - loader.growOffset) * (progress - 0.5) * 2;
    const qreal circumference = 2 * M_PI * loader.radius;
    const qreal start = offset / circumference * TurnDegrees + angle();
    const qreal span = qMax(length / circumference * TurnDegrees, 0.1);
    painter.drawArc(circle, qRound(-start * 16), qRound(-span * 16));
}

void Spinner::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    restart();
}

void Spinner::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    turn_->stop();
    dash_->stop();
}

void Spinner::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
        restart();
}

}
