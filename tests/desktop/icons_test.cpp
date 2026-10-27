#include "design_system/icons.h"
#include "icon_resource_check.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSvgRenderer>
#include <QtTest>
#include <algorithm>

namespace {
// Element markup copied from the upstream Lucide 1.61.0 files, independent of the assets.
QByteArray lucideSvg(const QColor& color, const QByteArray& elements) {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" "
           "stroke=\"" +
           color.name().toUtf8() +
           "\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" + elements +
           "</svg>";
}

QImage renderReference(const QByteArray& svg) {
    QPixmap expected(48, 48);
    expected.setDevicePixelRatio(2);
    expected.fill(Qt::transparent);
    QSvgRenderer reference(svg);
    QPainter painter(&expected);
    reference.render(&painter, QRectF(0, 0, 24, 24));
    painter.end();
    return expected.toImage();
}

const QByteArray playElements = "<path d=\"M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 "
                                ".003 3.458l-12 7A2 2 0 0 1 5 19z\"/>";
} // namespace

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
        QSvgRenderer reference(lucideSvg(Qt::black, playElements));
        reference.render(&vectorPainter, QRectF(0, 0, 24, 24));
        vectorPainter.end();
        QCOMPARE(actual, expected);
    }
    void lucideGlyphsMatchUpstreamInBothThemes() {
        using namespace choscordb::design;
        const QList<std::pair<Icon, QByteArray>> specimens{
            {Icon::Run, playElements},
            {Icon::Database, "<ellipse cx=\"12\" cy=\"5\" rx=\"9\" ry=\"3\"/>"
                             "<path d=\"M3 5V19A9 3 0 0 0 21 19V5\"/>"
                             "<path d=\"M3 12A9 3 0 0 0 21 12\"/>"},
            {Icon::Export, "<path d=\"M12 15V3\"/>"
                           "<path d=\"M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4\"/>"
                           "<path d=\"m7 10 5 5 5-5\"/>"},
            {Icon::Code, "<path d=\"m16 18 6-6-6-6\"/><path d=\"m8 6-6 6 6 6\"/>"},
            {Icon::Table, "<path d=\"M12 3v18\"/>"
                          "<rect width=\"18\" height=\"18\" x=\"3\" y=\"3\" rx=\"2\"/>"
                          "<path d=\"M3 9h18\"/><path d=\"M3 15h18\"/>"},
            {Icon::Folder, "<path d=\"M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9"
                           "L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z\"/>"},
            {Icon::File, "<path d=\"M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706"
                         "l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z\"/>"
                         "<path d=\"M14 2v5a1 1 0 0 0 1 1h5\"/>"},
            {Icon::Key,
             "<path d=\"m2 21 9.6-9.6\"/>"
             "<path d=\"m7.5 15.5 2.3 2.3a1 1 0 0 1 0 1.4l-2.1 2.1a1 1 0 0 1-1.4 0L4 19\"/>"
             "<circle cx=\"15.5\" cy=\"7.5\" r=\"5.5\"/>"},
            {Icon::Grid2x2, "<path d=\"M12 3v18\"/><path d=\"M3 12h18\"/>"
                            "<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/>"},
            {Icon::Cancel, "<rect width=\"18\" height=\"18\" x=\"3\" y=\"3\" rx=\"2\"/>"}};
        for (const auto& [role, elements] : specimens) {
            QVERIFY(choscordb::test::iconResourceDecodes(role));
            for (const QColor color : {QColor("#171717"), QColor("#fafafa")}) {
                const auto actual = themedIcon(role, color, 24).pixmap(QSize(24, 24), 2.0);
                QVERIFY2(!actual.isNull(), qPrintable(iconResourcePath(role)));
                QCOMPARE(actual.toImage(), renderReference(lucideSvg(color, elements)));
            }
        }
    }
    void rolesUseTheirLucideFiles() {
        using namespace choscordb::design;
        QCOMPARE(iconResourcePath(Icon::Settings), QString(":/icons/sliders-horizontal.svg"));
        QCOMPARE(iconResourcePath(Icon::Cancel), QString(":/icons/square.svg"));
        QCOMPARE(iconResourcePath(Icon::Square), QString(":/icons/square.svg"));
        QCOMPARE(iconResourcePath(Icon::Run), QString(":/icons/play.svg"));
        for (const auto& definition : iconCatalog()) {
            const auto stem = QFileInfo(iconResourcePath(definition.role)).completeBaseName();
            QCOMPARE(definition.name, stem);
            QCOMPARE(QFileInfo(definition.source).completeBaseName(), stem);
        }
    }
    void strokeCoverageMatchesLucideAtRetinaScale() {
        using namespace choscordb::design;
        const auto image =
            themedIcon(Icon::Add, Qt::black, 24).pixmap(QSize(24, 24), 2.0).toImage();
        // Lucide draws a 2px stroke. Away from the plus intersection and round
        // end caps, the two-times render covers 4 physical pixels.
        double coverage = 0;
        for (int y = 0; y < image.height(); ++y)
            coverage += image.pixelColor(16, y).alphaF();
        QVERIFY2(qAbs(coverage - 4.0) < 0.1, qPrintable(QString::number(coverage)));
    }
    void semanticAssetsRenderTheRequestedColorAndScale() {
        using namespace choscordb::design;
        for (const auto& definition : iconCatalog()) {
            const auto role = definition.role;
            if (role == Icon::AppMark || role == Icon::PostgreSQL || role == Icon::SQLite ||
                role == Icon::MySQL || role == Icon::MariaDB) {
                continue; // Brand identities keep their own colors, checked below.
            }
            for (const auto& color : {QColor("#171717"), QColor("#fafafa")}) {
                const auto icon = themedIcon(role, color, 20);
                const auto pixmap = icon.pixmap(QSize(20, 20), 2.0);
                QCOMPARE(pixmap.devicePixelRatio(), 2.0);
                QCOMPARE(pixmap.size(), QSize(40, 40));
                QVERIFY(choscordb::test::iconResourceDecodes(role));
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

    void brandLogosKeepTheirColorsAndSquareViewBoxesAcrossThemes() {
        using namespace choscordb::design;
        for (const auto role :
             {Icon::AppMark, Icon::PostgreSQL, Icon::SQLite, Icon::MySQL, Icon::MariaDB}) {
            QVERIFY(choscordb::test::iconResourceDecodes(role));
            const auto light = themedIcon(role, Qt::black, 32).pixmap(QSize(32, 32), 2.0);
            const auto dark = themedIcon(role, Qt::white, 32).pixmap(QSize(32, 32), 2.0);
            QCOMPARE(light.devicePixelRatio(), 2.0);
            QCOMPARE(light.size(), QSize(64, 64));
            QCOMPARE(light.toImage(), dark.toImage());
            QFile source(iconResourcePath(role));
            QVERIFY(source.open(QIODevice::ReadOnly));
            const auto viewBox = QSvgRenderer(source.readAll()).viewBoxF();
            QVERIFY2(qFuzzyCompare(viewBox.width(), viewBox.height()),
                     qPrintable(iconResourcePath(role)));
        }
    }

    void databaseLogosKeepDistinctBrandColorsAcrossThemes() {
        using namespace choscordb::design;
        QList<QImage> images;
        for (const auto role : {Icon::PostgreSQL, Icon::SQLite}) {
            const auto image = themedIcon(role, Qt::black, 32).pixmap(QSize(32, 32), 2.0).toImage();
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

    void mysqlUsesTheDeviconDolphinSvg() {
        using namespace choscordb::design;
        QCOMPARE(driverIcon("mysql"), Icon::MySQL);
        QCOMPARE(iconResourcePath(Icon::MySQL), QString(":/icons/mysql.svg"));
        const auto image =
            themedIcon(Icon::MySQL, Qt::white, 32).pixmap(QSize(32, 32), 2.0).toImage();
        int dolphinBlue = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const auto pixel = image.pixelColor(x, y);
                if (pixel.alpha() <= 200)
                    continue;
                // The devicon dolphin is a single #00618A fill.
                dolphinBlue += qAbs(pixel.red() - 0x00) < 12 && qAbs(pixel.green() - 0x61) < 12 &&
                               qAbs(pixel.blue() - 0x8a) < 12;
                QVERIFY2(!(pixel.red() > pixel.blue() + 50 && pixel.red() > pixel.green() + 25),
                         "MySQL wordmark orange must not appear in the icon");
            }
        QVERIFY2(dolphinBlue > 20, qPrintable(QString::number(dolphinBlue)));
        QFile artwork(iconResourcePath(Icon::MySQL));
        QVERIFY(artwork.open(QIODevice::ReadOnly));
        // Pin the unmodified devicon v2.18.0 icons/mysql/mysql-original.svg.
        QCOMPARE(QCryptographicHash::hash(artwork.readAll(), QCryptographicHash::Sha256).toHex(),
                 QByteArray("f73fa5d6b9da33fbf540ff96f5360c24b6d29d883ecfe6fcaecf89a8d3456067"));
    }

    void mariadbUsesTheBrownSealAcrossThemes() {
        using namespace choscordb::design;
        QCOMPARE(driverIcon("mariadb"), Icon::MariaDB);
        QCOMPARE(iconResourcePath(Icon::MariaDB), QString(":/icons/mariadb.svg"));
        QVERIFY(choscordb::test::iconResourceDecodes(Icon::MariaDB));
        const auto light = themedIcon(Icon::MariaDB, Qt::black, 32).pixmap(QSize(32, 32), 2.0);
        const auto dark = themedIcon(Icon::MariaDB, Qt::white, 32).pixmap(QSize(32, 32), 2.0);
        QCOMPARE(light.toImage(), dark.toImage());
        const auto image = light.toImage();
        bool brown = false;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const auto pixel = image.pixelColor(x, y);
                brown |= pixel.alpha() > 200 && qAbs(pixel.red() - 0xc0) < 12 &&
                         qAbs(pixel.green() - 0x76) < 12 && qAbs(pixel.blue() - 0x5a) < 12;
            }
        QVERIFY2(brown, "The MariaDB seal keeps its brand brown");
        QVERIFY(image !=
                themedIcon(Icon::MySQL, Qt::black, 32).pixmap(QSize(32, 32), 2.0).toImage());
    }

    void appIdentityKeepsWhitePawAndOrangeGradientAcrossThemes() {
        using namespace choscordb::design;
        QVERIFY(choscordb::test::iconResourceDecodes(Icon::AppMark));
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
        QVERIFY(names.contains("square"));
        // Cancel and Square share the Lucide square file and so its name.
        QCOMPARE(names.removeDuplicates(), 1);
    }

    void driversMapToTheirLogos() {
        QCOMPARE(choscordb::design::driverIcon("sqlite"), choscordb::design::Icon::SQLite);
        QCOMPARE(choscordb::design::driverIcon("postgres"), choscordb::design::Icon::PostgreSQL);
        QCOMPARE(choscordb::design::driverIcon("mysql"), choscordb::design::Icon::MySQL);
        QCOMPARE(choscordb::design::driverIcon("oracle"), choscordb::design::Icon::Database);
        QCOMPARE(choscordb::design::driverIcon({}), choscordb::design::Icon::Database);
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
