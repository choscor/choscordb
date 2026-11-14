#include "design_system/theme.h"
#include "preview_test.h"
#include "tools/preview/preview_window.h"

#include <QtTest>

namespace {
int countColor(const QImage& image, const QColor& color) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            count += image.pixelColor(x, y) == color;
    return count;
}
}

void PreviewTest::interactionStatesShowEachFamilyInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("interaction-states"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        const auto colors = choscordb::design::resolvedThemeForWidget(*host).colors;
        const auto sample = [host](const QString& id) {
            auto* widget = host->findChild<QWidget*>("state-" + id);
            return widget ? widget->grab().toImage() : QImage{};
        };
        for (const auto* family : {"field", "list-row", "table-cell", "tree-row", "pane-tab",
                                   "tool-button", "check-box"}) {
            const auto normal = sample(QString(family) + "-normal");
            QVERIFY2(!normal.isNull(), family);
            const bool kumoHover = QString(family) != "field" && QString(family) != "check-box";
            QVERIFY2((sample(QString(family) + "-hover") != normal) == kumoHover, family);
            QVERIFY2(sample(QString(family) + "-disabled") != normal, family);
            QVERIFY2(countColor(sample(QString(family) + "-focus"), colors.brand) > 20, family);
        }
        for (const auto* family : {"list-row", "table-cell", "tree-row"}) {
            const auto selected = sample(QString(family) + "-selected");
            QVERIFY2(countColor(selected, colors.tint) > selected.width(), family);
            const auto selectedHover = sample(QString(family) + "-selected-hover");
            QVERIFY2(countColor(selectedHover, colors.tint) > selectedHover.width(), family);
        }
        for (const auto* family : {"pane-tab"}) {
            const auto pressed = sample(QString(family) + "-pressed");
            QVERIFY2(pressed == sample(QString(family) + "-hover"), family);
        }
        QVERIFY(sample("check-box-checked") != sample("check-box-normal"));
        QVERIFY(sample("pane-tab-selected") != sample("pane-tab-normal"));
    }
}
