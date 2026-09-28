#include "preview_test.h"
#include "tools/preview/preview_window.h"

#include <QCheckBox>
#include <QSplitter>
#include <QtTest>

void PreviewTest::editorResultsSplitUsesEqualPanesInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("separators-splitters"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* splitter = host->findChild<QSplitter*>("previewEditorResultsSplit");
        QVERIFY(splitter);
        QCOMPARE(splitter->orientation(), Qt::Vertical);
        const auto sizes = splitter->sizes();
        QVERIFY(qAbs(sizes[0] - sizes[1]) <= 2);
    }
}

void PreviewTest::switchSpecimenUsesRealControlsInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("switches"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* off = host->findChild<QCheckBox*>("previewSwitchOff");
        auto* on = host->findChild<QCheckBox*>("previewSwitchOn");
        QVERIFY(off && on);
        QVERIFY(off->isVisible() && on->isVisible());
        QCOMPARE(off->property("designRole").toString(), QString("switch"));
        QCOMPARE(on->property("designRole").toString(), QString("switch"));
        QVERIFY(!off->isChecked());
        QVERIFY(on->isChecked());
    }
}
