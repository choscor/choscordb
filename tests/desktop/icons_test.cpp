#include "design_system/icons.h"

#include <QCryptographicHash>
#include <QFile>
#include <QPainter>
#include <QSvgRenderer>
#include <QtTest>
#include <algorithm>

class IconsTest final : public QObject {
    Q_OBJECT
  private slots:
    void vectorPaintingHonorsPainterScale_data() {
        QTest::addColumn<qreal>("deviceScale");
        QTest::addColumn<qreal>("painterScale");
        QTest::newRow("retina") << qreal(2) << qreal(1);
        QTest::newRow("fractional-transform") << qreal(1) << qreal(1.5);
        QTest::newRow("retina-fractional-transform") << qreal(2) << qreal(1.25);
    }
    void vectorPaintingHonorsPainterScale() {
        QFETCH(qreal, deviceScale);
        QFETCH(qreal, painterScale);
        const auto physical =
            QSizeF(24 * deviceScale * painterScale, 24 * deviceScale * painterScale).toSize();
        QImage actual(physical, QImage::Format_ARGB32_Premultiplied);
        actual.setDevicePixelRatio(deviceScale);
        actual.fill(Qt::transparent);
        QPainter painter(&actual);
        painter.scale(painterScale, painterScale);
        choscordb::design::themedIcon(choscordb::design::Icon::Run, Qt::black, 24)
            .paint(&painter, QRect(0, 0, 24, 24));
        painter.end();

        QImage expected(physical, QImage::Format_ARGB32_Premultiplied);
        expected.setDevicePixelRatio(deviceScale);
        expected.fill(Qt::transparent);
        QPainter vectorPainter(&expected);
        vectorPainter.scale(painterScale, painterScale);
        // Reference path, directly rasterized at the final device transform.
        QSvgRenderer reference(
            QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" "
                              "fill=\"none\" stroke=\"#000000\" stroke-width=\"1.6\" "
                              "stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                              "<path d=\"m7 4 14 8-14 8z\"/></svg>"));
        reference.render(&vectorPainter, QRectF(0, 0, 24, 24));
        vectorPainter.end();
        QCOMPARE(actual, expected);
    }
    void referenceGlyphSilhouettesMatchAuthority() {
        using namespace choscordb::design;
        // Literal paths from docs/mvp-design/prototype.js, independent of assets.
        const QList<std::pair<Icon, QByteArray>> specimens{
            {Icon::Run, "m7 4 14 8-14 8z"},
            {Icon::Database,
             "M4 6c0-4 16-4 16 0s-16 4-16 0m0 0v12c0 4 16 4 16 0V6M4 12c0 4 16 4 16 0"},
            {Icon::Export, "M12 3v12m-4-4 4 4 4-4M4 15v6h16v-6"}};
        for (const auto& [role, path] : specimens) {
            QPixmap expected(48, 48);
            expected.setDevicePixelRatio(2);
            expected.fill(Qt::transparent);
            QSvgRenderer renderer("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" "
                                  "fill=\"none\" stroke=\"#000000\" stroke-width=\"1.6\" "
                                  "stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"" +
                                  path + "\"/></svg>");
            QPainter painter(&expected);
            renderer.render(&painter, QRectF(0, 0, 24, 24));
            painter.end();
            QCOMPARE(themedIcon(role, Qt::black, 24).pixmap(QSize(24, 24), 2.0).toImage(),
                     expected.toImage());
        }
    }
    void explorerGlyphsRenderTheReferenceShapes() {
        using namespace choscordb::design;
        // Independent path literals from the approved prototype.js icon map.
        const QList<std::pair<QString, QByteArray>> specimens{
            {"code", "m8 6-6 6 6 6m8-12 6 6-6 6m-3-15-2 18"},
            {"table", "M3 4h18v16H3zM3 9h18M9 9v11"},
            {"folder", "M3 6h7l2 3h9v11H3z"},
            {"file", "M5 3h9l5 5v13H5zM14 3v6h5"},
            {"key", "M8 3a5 5 0 1 0 0 10A5 5 0 0 0 8 3m4 9 9 9m-5-5 3-3"}};
        const auto catalog = iconCatalog();
        for (const auto& [name, path] : specimens) {
            const auto found =
                std::find_if(catalog.cbegin(), catalog.cend(),
                             [&name](const auto& entry) { return entry.name == name; });
            QVERIFY2(found != catalog.cend(), qPrintable("Missing shared explorer icon: " + name));
            for (const QColor color : {QColor("#171717"), QColor("#fafafa")}) {
                QPixmap expected(48, 48);
                expected.setDevicePixelRatio(2);
                expected.fill(Qt::transparent);
                QSvgRenderer reference(
                    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" "
                    "fill=\"none\" stroke=\"" +
                    color.name().toUtf8() +
                    "\" stroke-width=\"1.6\" stroke-linecap=\"round\" "
                    "stroke-linejoin=\"round\"><path d=\"" +
                    path + "\"/></svg>");
                QPainter painter(&expected);
                reference.render(&painter, QRectF(0, 0, 24, 24));
                painter.end();
                const auto actual = themedIcon(found->role, color, 24).pixmap(QSize(24, 24), 2.0);
                QVERIFY2(!actual.isNull(), qPrintable(name));
                QCOMPARE(actual.toImage(), expected.toImage());
            }
        }
    }
    void strokeCoverageMatchesMvpAtRetinaScale() {
        using namespace choscordb::design;
        const auto image =
            themedIcon(Icon::Add, Qt::black, 24).pixmap(QSize(24, 24), 2.0).toImage();
        // The reference plus has a 1.6px stroke. Away from its intersection
        // and round end caps, the two-times render covers 3.2 physical pixels.
        double coverage = 0;
        for (int y = 0; y < image.height(); ++y)
            coverage += image.pixelColor(16, y).alphaF();
        QVERIFY2(qAbs(coverage - 3.2) < 0.1, qPrintable(QString::number(coverage)));
    }
    void semanticAssetsRenderTheRequestedColorAndScale() {
        using namespace choscordb::design;
        for (const auto& definition : iconCatalog()) {
            const auto role = definition.role;
            if (role == Icon::AppMark || role == Icon::PostgreSQL || role == Icon::SQLite ||
                role == Icon::MySQL) {
                continue; // Brand identities keep their own colors, checked below.
            }
            for (const auto& color : {QColor("#171717"), QColor("#fafafa")}) {
                const auto icon = themedIcon(role, color, 20);
                const auto pixmap = icon.pixmap(QSize(20, 20), 2.0);
                QCOMPARE(pixmap.devicePixelRatio(), 2.0);
                QCOMPARE(pixmap.size(), QSize(40, 40));
                QVERIFY(iconResourceDecodes(role));
                const auto image = pixmap.toImage();
                bool hasStroke = false;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        const auto pixel = image.pixelColor(x, y);
                        if (pixel.alpha() > 128) {
                            hasStroke = true;
                            QVERIFY(qAbs(pixel.red() - color.red()) <= 2);
                            QVERIFY(qAbs(pixel.green() - color.green()) <= 2);
                            QVERIFY(qAbs(pixel.blue() - color.blue()) <= 2);
                        }
                    }
                }
                QVERIFY(hasStroke);
            }
        }
    }

    void databaseLogosKeepDistinctBrandColorsAcrossThemes() {
        using namespace choscordb::design;
        QList<QImage> images;
        for (const auto role : {Icon::PostgreSQL, Icon::SQLite}) {
            QVERIFY(iconResourceDecodes(role));
            const auto light = themedIcon(role, Qt::black, 32).pixmap(QSize(32, 32), 2.0);
            const auto dark = themedIcon(role, Qt::white, 32).pixmap(QSize(32, 32), 2.0);
            QCOMPARE(light.devicePixelRatio(), 2.0);
            QCOMPARE(light.size(), QSize(64, 64));
            QCOMPARE(light.toImage(), dark.toImage());
            const auto image = light.toImage();
            bool hasBlue = false;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const auto pixel = image.pixelColor(x, y);
                    hasBlue |= pixel.alpha() > 128 && pixel.blue() > pixel.red() + 30;
                }
            QVERIFY(hasBlue);
            images.append(image);
        }
        QVERIFY(images.at(0) != images.at(1));
    }

    void mysqlUsesOfficialOfflineArtworkAcrossThemes() {
        using namespace choscordb::design;
        const auto catalog = iconCatalog();
        const auto found = std::find_if(catalog.cbegin(), catalog.cend(),
                                        [](const auto& entry) { return entry.name == "mysql"; });
        QVERIFY(found != catalog.cend());
        QVERIFY(iconResourceDecodes(found->role));
        QCOMPARE(iconResourcePath(found->role), QString(":/icons/mysql.png"));
        const auto light = themedIcon(found->role, Qt::black, 32).pixmap(QSize(32, 32), 2.0);
        const auto dark = themedIcon(found->role, Qt::white, 32).pixmap(QSize(32, 32), 2.0);
        QVERIFY(!light.isNull());
        QCOMPARE(light.toImage(), dark.toImage());
        QFile artwork(iconResourcePath(found->role));
        QVERIFY(artwork.open(QIODevice::ReadOnly));
        // Pin the unmodified artwork downloaded from mysql.com's logo download page.
        QCOMPARE(QCryptographicHash::hash(artwork.readAll(), QCryptographicHash::Sha256).toHex(),
                 QByteArray("2d59bc428916752528280eac03330d712164163e2f3c476409f5c25d8a7c2778"));
    }

    void appIdentityKeepsWhitePawAndOrangeGradientAcrossThemes() {
        using namespace choscordb::design;
        QVERIFY(iconResourceDecodes(Icon::AppMark));
        const auto dark = themedIcon(Icon::AppMark, QColor("#171717"), 128)
                              .pixmap(QSize(128, 128), 2.0)
                              .toImage();
        const auto light = themedIcon(Icon::AppMark, QColor("#fafafa"), 128)
                               .pixmap(QSize(128, 128), 2.0)
                               .toImage();
        QCOMPARE(dark, light);
        QCOMPARE(dark.size(), QSize(256, 256));
        QCOMPARE(dark.pixelColor(0, 0).alpha(), 0);
        const auto top = dark.pixelColor(128, 24);
        const auto bottom = dark.pixelColor(128, 232);
        QVERIFY(top.red() > 230 && top.green() > 60 && top.green() < 180);
        QVERIFY(bottom.red() > 230 && bottom.green() > 140 && bottom.green() < 230);
        QVERIFY(bottom.green() > top.green() + 30);

        // The Lucide paw-print is white and retains its four distinct pads.
        const QList<QPoint> pawStrokeSamples{{120, 48}, {176, 80}, {192, 144}, {91, 112}};
        for (const auto& point : pawStrokeSamples) {
            const auto pixel = dark.pixelColor(point);
            QVERIFY2(pixel.red() > 245 && pixel.green() > 245 && pixel.blue() > 245,
                     qPrintable(QStringLiteral("Expected white paw stroke at %1,%2")
                                    .arg(point.x())
                                    .arg(point.y())));
        }
    }

    void semanticCatalogIncludesApplicationActions() {
        using namespace choscordb::design;
        const auto catalog = iconCatalog();
        QVERIFY(!catalog.isEmpty());
        QStringList names;
        for (const auto& definition : catalog) {
            names.append(definition.name);
            QVERIFY(!definition.source.isEmpty());
            QVERIFY(QFile::exists(iconResourcePath(definition.role)));
        }
        QVERIFY(names.contains("search"));
        QVERIFY(names.contains("database"));
        QVERIFY(names.contains("mysql"));
        QVERIFY(names.contains("copy"));
        QVERIFY(names.contains("cancel"));
        QVERIFY(names.contains("square"));
        QCOMPARE(names.removeDuplicates(), 0);
    }

    void applicationIconAssetsAreAvailableOffline() {
        using namespace choscordb::design;
        // Load the resource collection through the public icon API.
        QVERIFY(!themedIcon(Icon::Add, Qt::black, 16).isNull());
        for (const auto* filename :
             {"refresh-cw.svg", "x.svg", "chevron-down.svg", "chevron-right.svg",
              "chevron-left.svg", "search.svg", "database.svg", "check.svg", "triangle-alert.svg",
              "circle-alert.svg", "loader-circle.svg", "copy.svg", "download.svg"}) {
            const auto path = QStringLiteral(":/icons/%1").arg(QLatin1String(filename));
            QVERIFY2(QFile::exists(path), qPrintable(path));
        }
    }
};

QTEST_MAIN(IconsTest)
#include "icons_test.moc"
