#include "design_system/icons.h"
#include "design_system/theme.h"

#include <QDebug>
#include <QHash>
#include <QIconEngine>
#include <QMutex>
#include <QPainter>
#include <QPixmap>
#include <QResource>
#include <QSvgRenderer>

#include <memory>
#include <utility>

int qInitResources_resources();

namespace choscordb::design {
namespace {

QByteArray themedSvg(Icon icon, const QColor& color) {
    // QResource only resolves the compiled :/icons namespace, never the filesystem.
    QResource source(iconResourcePath(icon));
    if (!source.isValid()) {
        return {};
    }
    auto svg = source.uncompressedData();
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

struct PixmapKey {
    QSize size;
    qreal scale = 1;
    bool operator==(const PixmapKey&) const = default;
};
size_t qHash(const PixmapKey& key, size_t seed = 0) {
    return qHashMulti(seed, key.size.width(), key.size.height(), key.scale);
}

class SvgIconEngine final : public QIconEngine {
  public:
    explicit SvgIconEngine(QByteArray svg) : svg_(std::move(svg)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode, QIcon::State) override {
        // Render into the final paint device/transform. An intermediate pixmap
        // cannot account for fractional painter scaling and softens SVG edges.
        if (!renderer_) // perf-ok: parsed once per engine, then reused.
            renderer_ = std::make_unique<QSvgRenderer>(svg_);
        renderer_->render(painter, QRectF(rect));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode, QIcon::State, qreal scale) override {
        // Delegates request the same few sizes for every row; rasterize each once.
        const PixmapKey key{size, scale};
        if (const auto found = pixmaps_.constFind(key); found != pixmaps_.cend())
            return found.value();
        auto rendered = renderSvg(svg_, size, scale);
        if (pixmaps_.size() >= 16)
            pixmaps_.clear();
        pixmaps_.insert(key, rendered);
        return rendered;
    }

    [[nodiscard]] QIconEngine* clone() const override { return new SvgIconEngine(svg_); }
    [[nodiscard]] bool isNull() override { return svg_.isEmpty(); }
    [[nodiscard]] QString key() const override { return QStringLiteral("ChoscorDBSvgIcon"); }

  private:
    QByteArray svg_;
    std::unique_ptr<QSvgRenderer> renderer_;
    QHash<PixmapKey, QPixmap> pixmaps_;
};

} // namespace

QList<IconDefinition> iconCatalog() {
    ::qInitResources_resources();
    return {
        {Icon::Code, QStringLiteral("code"), QStringLiteral("desktop/resources/icons/code.svg")},
        {Icon::Table, QStringLiteral("table"), QStringLiteral("desktop/resources/icons/table.svg")},
        {Icon::Grid2x2, QStringLiteral("grid-2x2"),
         QStringLiteral("desktop/resources/icons/grid-2x2.svg")},
        {Icon::Folder, QStringLiteral("folder"),
         QStringLiteral("desktop/resources/icons/folder.svg")},
        {Icon::File, QStringLiteral("file"), QStringLiteral("desktop/resources/icons/file.svg")},
        {Icon::Key, QStringLiteral("key"), QStringLiteral("desktop/resources/icons/key.svg")},
        {Icon::Link, QStringLiteral("link"), QStringLiteral("desktop/resources/icons/link.svg")},
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
    case Icon::Grid2x2:
        return QStringLiteral(":/icons/grid-2x2.svg");
    case Icon::Folder:
        return QStringLiteral(":/icons/folder.svg");
    case Icon::File:
        return QStringLiteral(":/icons/file.svg");
    case Icon::Key:
        return QStringLiteral(":/icons/key.svg");
    case Icon::Link:
        return QStringLiteral(":/icons/link.svg");
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

namespace {
struct IconKey {
    Icon icon;
    QRgb color;
    int size;
    double strokeWidth;
    bool operator==(const IconKey&) const = default;
};
size_t qHash(const IconKey& key, size_t seed = 0) {
    return qHashMulti(seed, static_cast<int>(key.icon), key.color, key.size, key.strokeWidth);
}
QIcon createThemedIcon(Icon icon, const QColor& color, int size) {
    if (icon == Icon::MySQL) {
        // The upstream raster includes a wordmark beneath the dolphin. Compact
        // engine badges need the dolphin alone; keep the original asset intact.
        auto mark = QImage(iconResourcePath(icon)).copy(QRect(101, 0, 74, 72));
        for (int y = 68; y < mark.height(); ++y)
            for (int x = 0; x < 49; ++x)
                mark.setPixelColor(x, y, Qt::transparent);
        return QIcon(QPixmap::fromImage(mark));
    }
    auto svg = themedSvg(icon, color);
    if (!renderSvg(svg, QSize(size, size), 1.0).isNull()) {
        return QIcon(new SvgIconEngine(std::move(svg)));
    }
    qWarning() << "Could not render required SVG icon:" << iconResourcePath(icon);
    return {};
}
} // namespace

QIcon themedIcon(Icon icon, const QColor& color, int size) {
    ::qInitResources_resources();
    // Item delegates request icons per painted row. Parsing and validating the SVG
    // each time dominates large trees, so reuse icons for identical inputs. The
    // stroke width is part of the key because metrics can change it at run time.
    static QMutex mutex;
    static QHash<IconKey, QIcon> cache;
    const IconKey key{icon, color.rgba(), size, iconStrokeWidth()};
    const QMutexLocker locker(&mutex);
    if (const auto found = cache.constFind(key); found != cache.cend())
        return found.value();
    auto created = createThemedIcon(icon, color, size);
    if (cache.size() >= 512)
        cache.clear();
    cache.insert(key, created);
    return created;
}

Icon driverIcon(const QString& driver) {
    if (driver == QLatin1String("sqlite"))
        return Icon::SQLite;
    if (driver == QLatin1String("postgres"))
        return Icon::PostgreSQL;
    if (driver == QLatin1String("mysql"))
        return Icon::MySQL;
    return Icon::Database;
}
} // namespace choscordb::design
