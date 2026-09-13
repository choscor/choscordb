#include "design_system/icons.h"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>
#include <QtTest>

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

    void semanticCatalogIncludesApplicationActions() {
        using namespace choscordb::design;
        const auto catalog = iconCatalog();
        QCOMPARE(catalog.size(), 17);
        QStringList names;
        for (const auto& definition : catalog) {
            names.append(definition.name);
            QVERIFY(!definition.source.isEmpty());
            QVERIFY(QFile::exists(iconResourcePath(definition.role)));
        }
        QVERIFY(names.contains("search"));
        QVERIFY(names.contains("database"));
        QVERIFY(names.contains("copy"));
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
