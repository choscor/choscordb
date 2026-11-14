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
QByteArray phosphorSvg(const QColor& color, const QByteArray& path) {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 256 256\" fill=\"" +
           color.name().toUtf8() + "\"><path d=\"" + path + "\"/></svg>";
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

const QByteArray playPath =
    "M232.4,114.49,88.32,26.35a16,16,0,0,0-16.2-.3A15.86,15.86,0,0,0,64,39.87"
    "V216.13A15.94,15.94,0,0,0,80,232a16.07,16.07,0,0,0,8.36-2.35L232.4,"
    "141.51a15.81,15.81,0,0,0,0-27ZM80,215.94V40l143.83,88Z";
}

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
        QSvgRenderer reference(phosphorSvg(Qt::black, playPath));
        reference.render(&vectorPainter, QRectF(0, 0, 24, 24));
        vectorPainter.end();
        QCOMPARE(actual, expected);
    }
    void phosphorGlyphsMatchUpstreamInBothThemes() {
        using namespace choscordb::design;
        const QList<std::pair<Icon, QByteArray>> specimens{
            {Icon::Run, playPath},
            {Icon::Cancel,
             "M200,40H56A16,16,0,0,0,40,56V200a16,16,0,0,0,16,16H200a16,16,0,0,0,16-16"
             "V56A16,16,0,0,0,200,40Zm0,160H56V56H200V200Z"},
            {Icon::Close, "M205.66,194.34a8,8,0,0,1-11.32,11.32L128,139.31,61.66,205.66a8,8,0,0,1-"
                          "11.32-11.32L116.69,128,50.34,61.66A8,8,0,0,1,61.66,50.34L128,116.69l66."
                          "34-66.35a8,8,0,0,1,11.32,11.32L139.31,128Z"},
            {Icon::Check,
             "M229.66,77.66l-128,128a8,8,0,0,1-11.32,0l-56-56a8,8,0,0,1,11.32-11.32L96,"
             "188.69,218.34,66.34a8,8,0,0,1,11.32,11.32Z"},
            {Icon::CaretDown, "M213.66,101.66l-80,80a8,8,0,0,1-11.32,0l-80-80A8,8,0,0,1,53.66,90.34"
                              "L128,164.69l74.34-74.35a8,8,0,0,1,11.32,11.32Z"}};
        for (const auto& [role, path] : specimens) {
            QVERIFY(choscordb::test::iconResourceDecodes(role));
            for (const QColor color : {QColor("#171717"), QColor("#fafafa")}) {
                const auto actual = themedIcon(role, color, 24).pixmap(QSize(24, 24), 2.0);
                QVERIFY2(!actual.isNull(), qPrintable(iconResourcePath(role)));
                QCOMPARE(actual.toImage(), renderReference(phosphorSvg(color, path)));
            }
        }
    }
    void rolesUseTheKumoPhosphorFiles() {
        using namespace choscordb::design;
        const QList<std::pair<Icon, QString>> expected{{Icon::Run, "play"},
                                                       {Icon::Cancel, "stop"},
                                                       {Icon::Square, "square"},
                                                       {Icon::Prohibit, "prohibit"},
                                                       {Icon::Add, "plus"},
                                                       {Icon::Refresh, "arrow-clockwise"},
                                                       {Icon::Close, "x"},
                                                       {Icon::CaretDown, "caret-down"},
                                                       {Icon::CaretRight, "caret-right"},
                                                       {Icon::CaretLeft, "caret-left"},
                                                       {Icon::CaretUpDown, "caret-up-down"},
                                                       {Icon::Search, "magnifying-glass"},
                                                       {Icon::Database, "database"},
                                                       {Icon::Check, "check"},
                                                       {Icon::CheckCircle, "check-circle"},
                                                       {Icon::Settings, "sliders-horizontal"},
                                                       {Icon::Warning, "warning"},
                                                       {Icon::Error, "warning-octagon"},
                                                       {Icon::Loader, "circle-notch"},
                                                       {Icon::Copy, "copy"},
                                                       {Icon::Export, "download-simple"},
                                                       {Icon::Code, "code"},
                                                       {Icon::Table, "table"},
                                                       {Icon::GridFour, "grid-four"},
                                                       {Icon::Folder, "folder"},
                                                       {Icon::File, "file"},
                                                       {Icon::Key, "key"},
                                                       {Icon::Link, "link"},
                                                       {Icon::Eye, "eye"},
                                                       {Icon::EyeSlash, "eye-slash"},
                                                       {Icon::Trash, "trash"},
                                                       {Icon::ArrowUp, "arrow-up"},
                                                       {Icon::ArrowDown, "arrow-down"}};
        for (const auto& [role, name] : expected)
            QCOMPARE(iconResourcePath(role), QStringLiteral(":/icons/%1.svg").arg(name));
        for (const auto& definition : iconCatalog()) {
            const auto stem = QFileInfo(iconResourcePath(definition.role)).completeBaseName();
            QCOMPARE(definition.name, stem);
            QCOMPARE(QFileInfo(definition.source).completeBaseName(), stem);
        }
    }
    void regularWeightCoverageMatchesPhosphorAtRetinaScale() {
        using namespace choscordb::design;
        const auto image =
            themedIcon(Icon::Add, Qt::black, 24).pixmap(QSize(24, 24), 2.0).toImage();
        double coverage = 0;
        for (int y = 0; y < image.height(); ++y)
            coverage += image.pixelColor(16, y).alphaF();
        QVERIFY2(qAbs(coverage - 3.0) < 0.1, qPrintable(QString::number(coverage)));
    }
    void semanticAssetsRenderTheRequestedColorAndScale() {
        using namespace choscordb::design;
        for (const auto& definition : iconCatalog()) {
            const auto role = definition.role;
            if (role == Icon::AppMark || role == Icon::PostgreSQL || role == Icon::SQLite ||
                role == Icon::MySQL || role == Icon::MariaDB) {
                continue;
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
                dolphinBlue += qAbs(pixel.red() - 0x00) < 12 && qAbs(pixel.green() - 0x61) < 12 &&
                               qAbs(pixel.blue() - 0x8a) < 12;
                QVERIFY2(!(pixel.red() > pixel.blue() + 50 && pixel.red() > pixel.green() + 25),
                         "MySQL wordmark orange must not appear in the icon");
            }
        QVERIFY2(dolphinBlue > 20, qPrintable(QString::number(dolphinBlue)));
        QFile artwork(iconResourcePath(Icon::MySQL));
        QVERIFY(artwork.open(QIODevice::ReadOnly));
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

        const QList<QPoint> pawStrokeSamples{{63, 115}, {105, 73}, {150, 73}, {193, 115}};
        for (const auto& point : pawStrokeSamples) {
            const auto pixel = dark.pixelColor(point);
            QVERIFY2(
                pixel.red() > 245 && pixel.green() > 245 && pixel.blue() > 245,
                qPrintable(
                    QStringLiteral("Expected white paw at %1,%2").arg(point.x()).arg(point.y())));
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
        QVERIFY(names.contains("magnifying-glass"));
        QVERIFY(names.contains("database"));
        QVERIFY(names.contains("mysql"));
        QVERIFY(names.contains("copy"));
        QVERIFY(names.contains("square"));
        QCOMPARE(names.removeDuplicates(), 0);
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
        QVERIFY(!themedIcon(Icon::Add, Qt::black, 16).isNull());
        for (const auto* filename :
             {"arrow-clockwise.svg", "x.svg", "caret-down.svg", "caret-right.svg", "caret-left.svg",
              "magnifying-glass.svg", "database.svg", "check.svg", "check-circle.svg",
              "warning.svg", "warning-octagon.svg", "circle-notch.svg", "copy.svg",
              "download-simple.svg"}) {
            const auto path = QStringLiteral(":/icons/%1").arg(QLatin1String(filename));
            QVERIFY2(QFile::exists(path), qPrintable(path));
        }
    }
};

QTEST_MAIN(IconsTest)
#include "icons_test.moc"
