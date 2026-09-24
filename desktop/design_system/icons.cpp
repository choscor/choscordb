#include "design_system/icons.h"
#include "design_system/theme.h"

#include <QDebug>
#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
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
    // Database logos retain their upstream brand colors in both themes.
    if (icon == Icon::PostgreSQL || icon == Icon::SQLite) {
        return svg;
    }
    const auto replacement = color.name(QColor::HexRgb).toUtf8();
    svg.replace("currentColor", replacement);
    svg.replace("stroke-width=\"2\"",
                "stroke-width=\"" + QByteArray::number(iconStrokeWidth()) + "\"");
    svg.replace("#334155", replacement);
    svg.replace("#2F7DD3", replacement);
    return svg;
}

QPixmap renderSvg(const QByteArray& svg, const QSize& logicalSize, qreal scale) {
    if (logicalSize.isEmpty() || scale <= 0) {
        return {};
    }
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        return {};
    }
    QPixmap result((QSizeF(logicalSize) * scale).toSize());
    result.fill(Qt::transparent);
    result.setDevicePixelRatio(scale);
    QPainter painter(&result);
    renderer.render(&painter, QRectF(QPointF{}, QSizeF(logicalSize)));
    return result;
}

class SvgIconEngine final : public QIconEngine {
  public:
    explicit SvgIconEngine(QByteArray svg) : svg_(std::move(svg)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode, QIcon::State) override {
        // Render into the final paint device/transform. An intermediate pixmap
        // cannot account for fractional painter scaling and softens SVG edges.
        QSvgRenderer renderer(svg_);
        renderer.render(painter, QRectF(rect));
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
            root = reader.name() == QLatin1String("svg") && !QSvgRenderer(svg).viewBoxF().isEmpty();
        } else if (reader.name() == QLatin1String("path") ||
                   reader.name() == QLatin1String("polygon") ||
                   reader.name() == QLatin1String("rect") ||
                   reader.name() == QLatin1String("circle") ||
                   reader.name() == QLatin1String("ellipse") ||
                   reader.name() == QLatin1String("line") ||
                   reader.name() == QLatin1String("polyline")) {
            drawingElement = true;
        }
    }
    return !reader.hasError() && root && drawingElement;
}

} // namespace

QList<IconDefinition> iconCatalog() {
    ::qInitResources_resources();
    return {
        {Icon::Code, QStringLiteral("code"), QStringLiteral("desktop/resources/icons/code.svg")},
        {Icon::Table, QStringLiteral("table"), QStringLiteral("desktop/resources/icons/table.svg")},
        {Icon::Folder, QStringLiteral("folder"),
         QStringLiteral("desktop/resources/icons/folder.svg")},
        {Icon::File, QStringLiteral("file"), QStringLiteral("desktop/resources/icons/file.svg")},
        {Icon::Key, QStringLiteral("key"), QStringLiteral("desktop/resources/icons/key.svg")},
        {Icon::Eye, QStringLiteral("eye"), QStringLiteral("desktop/resources/icons/eye.svg")},
        {Icon::EyeOff, QStringLiteral("eye-off"),
         QStringLiteral("desktop/resources/icons/eye-off.svg")},
        {Icon::AppMark, QStringLiteral("app-mark"),
         QStringLiteral("desktop/resources/icons/app-mark.svg")},
        {Icon::Run, QStringLiteral("play"), QStringLiteral("desktop/resources/icons/play.svg")},
        {Icon::Cancel, QStringLiteral("cancel"),
         QStringLiteral("desktop/resources/icons/square.svg")},
        {Icon::Add, QStringLiteral("plus"), QStringLiteral("desktop/resources/icons/plus.svg")},
        {Icon::Refresh, QStringLiteral("refresh-cw"),
         QStringLiteral("desktop/resources/icons/refresh-cw.svg")},
        {Icon::Close, QStringLiteral("x"), QStringLiteral("desktop/resources/icons/x.svg")},
        {Icon::Square, QStringLiteral("square"),
         QStringLiteral("desktop/resources/icons/square.svg")},
        {Icon::ChevronDown, QStringLiteral("chevron-down"),
         QStringLiteral("desktop/resources/icons/chevron-down.svg")},
        {Icon::ChevronRight, QStringLiteral("chevron-right"),
         QStringLiteral("desktop/resources/icons/chevron-right.svg")},
        {Icon::ChevronLeft, QStringLiteral("chevron-left"),
         QStringLiteral("desktop/resources/icons/chevron-left.svg")},
        {Icon::Search, QStringLiteral("search"),
         QStringLiteral("desktop/resources/icons/search.svg")},
        {Icon::PostgreSQL, QStringLiteral("postgresql"),
         QStringLiteral("desktop/resources/icons/postgresql.svg")},
        {Icon::MySQL, QStringLiteral("mysql"), QStringLiteral("desktop/resources/icons/mysql.png")},
        {Icon::SQLite, QStringLiteral("sqlite"),
         QStringLiteral("desktop/resources/icons/sqlite.svg")},
        {Icon::Database, QStringLiteral("database"),
         QStringLiteral("desktop/resources/icons/database.svg")},
        {Icon::Check, QStringLiteral("check"), QStringLiteral("desktop/resources/icons/check.svg")},
        {Icon::Commit, QStringLiteral("commit"),
         QStringLiteral("desktop/resources/icons/commit.svg")},
        {Icon::Rollback, QStringLiteral("rollback"),
         QStringLiteral("desktop/resources/icons/rollback.svg")},
        {Icon::Settings, QStringLiteral("settings"),
         QStringLiteral("desktop/resources/icons/settings.svg")},
        {Icon::Warning, QStringLiteral("triangle-alert"),
         QStringLiteral("desktop/resources/icons/triangle-alert.svg")},
        {Icon::Error, QStringLiteral("circle-alert"),
         QStringLiteral("desktop/resources/icons/circle-alert.svg")},
        {Icon::Loader, QStringLiteral("loader-circle"),
         QStringLiteral("desktop/resources/icons/loader-circle.svg")},
        {Icon::Copy, QStringLiteral("copy"), QStringLiteral("desktop/resources/icons/copy.svg")},
        {Icon::Export, QStringLiteral("download"),
         QStringLiteral("desktop/resources/icons/download.svg")},
    };
}

QString iconResourcePath(Icon icon) {
    switch (icon) {
    case Icon::Code:
        return QStringLiteral(":/icons/code.svg");
    case Icon::Table:
        return QStringLiteral(":/icons/table.svg");
    case Icon::Folder:
        return QStringLiteral(":/icons/folder.svg");
    case Icon::File:
        return QStringLiteral(":/icons/file.svg");
    case Icon::Key:
        return QStringLiteral(":/icons/key.svg");
    case Icon::Eye:
        return QStringLiteral(":/icons/eye.svg");
    case Icon::EyeOff:
        return QStringLiteral(":/icons/eye-off.svg");
    case Icon::AppMark:
        return QStringLiteral(":/icons/app-mark.svg");
    case Icon::Run:
        return QStringLiteral(":/icons/play.svg");
    case Icon::Cancel:
        return QStringLiteral(":/icons/square.svg");
    case Icon::Add:
        return QStringLiteral(":/icons/plus.svg");
    case Icon::Refresh:
        return QStringLiteral(":/icons/refresh-cw.svg");
    case Icon::Close:
        return QStringLiteral(":/icons/x.svg");
    case Icon::Square:
        return QStringLiteral(":/icons/square.svg");
    case Icon::ChevronDown:
        return QStringLiteral(":/icons/chevron-down.svg");
    case Icon::ChevronRight:
        return QStringLiteral(":/icons/chevron-right.svg");
    case Icon::ChevronLeft:
        return QStringLiteral(":/icons/chevron-left.svg");
    case Icon::Search:
        return QStringLiteral(":/icons/search.svg");
    case Icon::PostgreSQL:
        return QStringLiteral(":/icons/postgresql.svg");
    case Icon::MySQL:
        return QStringLiteral(":/icons/mysql.png");
    case Icon::SQLite:
        return QStringLiteral(":/icons/sqlite.svg");
    case Icon::Database:
        return QStringLiteral(":/icons/database.svg");
    case Icon::Check:
        return QStringLiteral(":/icons/check.svg");
    case Icon::Commit:
        return QStringLiteral(":/icons/commit.svg");
    case Icon::Rollback:
        return QStringLiteral(":/icons/rollback.svg");
    case Icon::Settings:
        return QStringLiteral(":/icons/settings.svg");
    case Icon::Warning:
        return QStringLiteral(":/icons/triangle-alert.svg");
    case Icon::Error:
        return QStringLiteral(":/icons/circle-alert.svg");
    case Icon::Loader:
        return QStringLiteral(":/icons/loader-circle.svg");
    case Icon::Copy:
        return QStringLiteral(":/icons/copy.svg");
    case Icon::Export:
        return QStringLiteral(":/icons/download.svg");
    }
    return {};
}

bool iconResourceDecodes(Icon icon) {
    ::qInitResources_resources();
    if (icon == Icon::MySQL)
        return !QPixmap(iconResourcePath(icon)).isNull();
    return validSvgDocument(themedSvg(icon, QColor(Qt::black)));
}

QIcon themedIcon(Icon icon, const QColor& color, int size) {
    ::qInitResources_resources();
    if (icon == Icon::MySQL)
        return QIcon(iconResourcePath(icon));
    auto svg = themedSvg(icon, color);
    if (!renderSvg(svg, QSize(size, size), 1.0).isNull()) {
        return QIcon(new SvgIconEngine(std::move(svg)));
    }
    qWarning() << "Could not render required SVG icon:" << iconResourcePath(icon);
    return {};
}

} // namespace choscordb::design
