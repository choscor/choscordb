#include "design_system/icons.h"

#include <QFile>
#include <QtTest>

class IconsTest final : public QObject {
    Q_OBJECT
  private slots:
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
