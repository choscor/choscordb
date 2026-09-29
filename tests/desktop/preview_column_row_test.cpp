#include "design_system/column_row/column_row.h"
#include "preview_test.h"
#include "tools/preview/preview_window.h"
#include <QTreeView>
#include <QtTest>

void PreviewTest::columnRowSpecimenUsesRealDelegateInBothThemes() {
    using namespace choscordb::design;
    PreviewWindow window;
    QVERIFY(window.selectSpecimen("column-row"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* tree = host->findChild<QTreeView*>("previewColumnRows");
        QVERIFY(tree && tree->isVisible());
        QVERIFY(dynamic_cast<ColumnRowDelegate*>(tree->itemDelegate()));
        QCOMPARE(tree->model()->rowCount(), 4);
        QCOMPARE(tree->model()->index(0, 0).data().toString(), QString("customer_id"));
        QCOMPARE(tree->model()->index(0, 0).data(Qt::UserRole + 1).toString(), QString("INTEGER"));
        QCOMPARE(tree->model()->index(2, 0).data(Qt::UserRole + 1).toString(),
                 QString("timestamp(6) with time zone and extended suffix"));
        QVERIFY(tree->visualRect(tree->model()->index(3, 0)).isValid());
    }
}
