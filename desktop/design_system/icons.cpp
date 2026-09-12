#include "design_system/icons.h"

#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QXmlStreamReader>

#include <utility>

int qInitResources_resources();

namespace choscordb::design {
namespace {

QByteArray themedSvg(Icon icon, const QColor& color) {
    QFile source(iconResourcePath(icon));
    if (!source.open(QIODevice::ReadOnly)) {
        return {};
    }
    auto svg = source.readAll();
    const auto replacement = color.name(QColor::HexRgb).toUtf8();
    svg.replace("#334155", replacement);
    svg.replace("#2F7DD3", replacement);
    return svg;
}

QPixmap renderSvg(QByteArray svg, const QSize& logicalSize, qreal scale) {
    const auto pixels = (QSizeF(logicalSize) * scale).toSize();
    const auto dimensions = QByteArray("<svg width=\"") + QByteArray::number(pixels.width()) +
                            "\" height=\"" + QByteArray::number(pixels.height()) + "\" ";
    svg.replace("<svg ", dimensions);
    QPixmap result;
    if (!result.loadFromData(svg, "SVG")) {
        return {};
    }
    result.setDevicePixelRatio(scale);
    return result;
}

class SvgIconEngine final : public QIconEngine {
  public:
    explicit SvgIconEngine(QByteArray svg) : svg_(std::move(svg)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode, QIcon::State) override {
        const auto scale = painter->device()->devicePixelRatioF();
        painter->drawPixmap(rect, renderSvg(svg_, rect.size(), scale));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode, QIcon::State) override {
        return renderSvg(svg_, size, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode, QIcon::State, qreal scale) override {
        return renderSvg(svg_, size, scale);
    }

    [[nodiscard]] QIconEngine* clone() const override { return new SvgIconEngine(svg_); }
    [[nodiscard]] bool isNull() override { return svg_.isEmpty(); }
    [[nodiscard]] QString key() const override { return QStringLiteral("ChoscorDBSvgIcon"); }

  private:
    QByteArray svg_;
};

void paintSemanticIcon(QPainter& painter, const QRect& rect, Icon icon, const QColor& color) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.translate(rect.topLeft());
    painter.scale(rect.width() / 24.0, rect.height() / 24.0);
    if (icon == Icon::Run) {
        QPainterPath path;
        path.moveTo(7, 4);
        path.lineTo(20, 12);
        path.lineTo(7, 20);
        path.closeSubpath();
        painter.drawPath(path);
    } else if (icon == Icon::Cancel) {
        painter.drawRoundedRect(QRectF(4, 4, 16, 16), 2, 2);
    } else if (icon == Icon::Add) {
        painter.drawLine(QPointF(5, 12), QPointF(19, 12));
        painter.drawLine(QPointF(12, 5), QPointF(12, 19));
    } else {
        painter.drawEllipse(QRectF(5, 3, 14, 7));
        painter.drawArc(QRectF(5, 7, 14, 7), 180 * 16, 180 * 16);
        painter.drawArc(QRectF(5, 12, 14, 7), 180 * 16, 180 * 16);
        painter.drawArc(QRectF(11, 10, 10, 10), 65 * 16, 230 * 16);
    }
    painter.restore();
}

class SemanticIconEngine final : public QIconEngine {
  public:
    SemanticIconEngine(Icon icon, QColor color) : icon_(icon), color_(std::move(color)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode, QIcon::State) override {
        paintSemanticIcon(*painter, rect, icon_, color_);
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode, QIcon::State, qreal scale) override {
        QPixmap result((QSizeF(size) * scale).toSize());
        result.fill(Qt::transparent);
        result.setDevicePixelRatio(scale);
        QPainter painter(&result);
        paintSemanticIcon(painter, QRect(QPoint{}, size), icon_, color_);
        return result;
    }

    [[nodiscard]] QIconEngine* clone() const override {
        return new SemanticIconEngine(icon_, color_);
    }
    [[nodiscard]] bool isNull() override { return false; }
    [[nodiscard]] QString key() const override { return QStringLiteral("ChoscorDBSemanticIcon"); }

  private:
    Icon icon_;
    QColor color_;
};

bool validSvgDocument(const QByteArray& svg) {
    QXmlStreamReader reader(svg);
    bool root = false;
    bool drawingElement = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) {
            continue;
        }
        if (!root) {
            root =
                reader.name() == QLatin1String("svg") &&
                reader.attributes().value(QLatin1String("viewBox")) == QLatin1String("0 0 24 24");
        } else if (reader.name() == QLatin1String("path") ||
                   reader.name() == QLatin1String("polygon") ||
                   reader.name() == QLatin1String("rect")) {
            drawingElement = true;
        }
    }
    return !reader.hasError() && root && drawingElement;
}

} // namespace

QString iconResourcePath(Icon icon) {
    switch (icon) {
    case Icon::AppMark:
        return QStringLiteral(":/icons/app-mark.svg");
    case Icon::Run:
        return QStringLiteral(":/icons/play.svg");
    case Icon::Cancel:
        return QStringLiteral(":/icons/square.svg");
    case Icon::Add:
        return QStringLiteral(":/icons/plus.svg");
    }
    return {};
}

bool iconResourceDecodes(Icon icon) {
    ::qInitResources_resources();
    return validSvgDocument(themedSvg(icon, QColor(Qt::black)));
}

QIcon themedIcon(Icon icon, const QColor& color, int size) {
    ::qInitResources_resources();
    auto svg = themedSvg(icon, color);
    if (!renderSvg(svg, QSize(size, size), 1.0).isNull()) {
        return QIcon(new SvgIconEngine(std::move(svg)));
    }
    // A deployment without the SVG image plug-in still preserves the action's identity.
    return QIcon(new SemanticIconEngine(icon, color));
}

} // namespace choscordb::design
